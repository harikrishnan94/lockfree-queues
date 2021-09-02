#include <linux/rseq.h>

#include "lockfree-queue/mpsc_pc.h"
#include "rseq.h"

#define NO_SANITIZE(type) __attribute__((no_sanitize(#type)))

namespace lockfree
{
	BOOST_NOINLINE auto create_crit_section() noexcept -> const volatile rseq_cs*
	{
		static thread_local RestartableSequence rseq;
		static const auto* cs = [] {
			rseq_cs* cs = nullptr;
			// NOLINTNEXTLINE
			asm volatile("leaq " NAME(cs) "(%%rip), %[cs]\n\t" : [cs] "=r"(cs));
			return cs;
		}();

		return cs;
	}

	// ASM Utility Macros
	// NOLINTNEXTLINE
	asm(R"(
.macro align_up ptr:req, alignment:req
    sub $1, \alignment
    add \alignment, \ptr
    not \alignment
    and \alignment, \ptr
.endm

.macro is_align ptr:req, align:req, tmp:req
    mov \ptr, \tmp
    shr \align, \tmp
    shl \align, \tmp
    cmp \tmp, \ptr
.endm

.macro memcpy dst_ptr:req, src_ptr:req, sz:req, tmp:req, xmm:req
	is_align \dst_ptr, $4, \tmp
    jne 2f
    is_align \src_ptr, $4, \tmp
    jne 2f

1:
    cmp $16, \sz
    jb 3f
    movdqa (\src_ptr), \xmm
    movdqa \xmm, (\dst_ptr)
    addq $16, \dst_ptr
    addq $16, \src_ptr
    subq $16, \sz
    jmp 1b

2:
    cmp $16, \sz
    jb 3f
    movdqu (\src_ptr), \xmm
    movdqu \xmm, (\dst_ptr)
    addq $16, \dst_ptr
    addq $16, \src_ptr
    subq $16, \sz
    jmp 2b

3:
    cmp $1, \sz
    jb 4f
    mov (\src_ptr), \tmp\()b
    mov \tmp\()b, (\dst_ptr)
    addq $1, \dst_ptr
    addq $1, \src_ptr
    subq $1, \sz
    jmp 3b

4:
.endm
	)");

	// NOLINTNEXTLINE
	asm(R"(
// cpu_id must be loaded in 'eax'. rax and rdx are clobbered.
// Result: queue
.macro get_queue queue:req, mpsc_sz:req, per_cpu_qsz:req
    lea (\queue, \mpsc_sz), \queue
    mul \per_cpu_qsz
    lea (\queue, %rax), \queue
.endm

.macro load_head_ptr_and_tail queue_ptr:req, head_off:req, tail_off:req, out_head_ptr:req, out_head:req, out_tail:req
    lea (\queue_ptr, \head_off), \out_head_ptr
    mov (\out_head_ptr), \out_head
    mov (\queue_ptr, \tail_off), \out_tail
.endm

// Result: head
.macro compute_newhead head:req, elemsize:req, qword_sz:req
    lea (\head, \elemsize), \head
    add \qword_sz, \head
.endm

.macro get_queue_data queue:req, qsz:req, align:req
    add \qsz, \queue
    align_up \queue, \align
.endm

// XXX: All inputs, except `rb_ptr` are clobbered.
.macro copy_to_ring_buf rb_ptr:req, rb_head:req, rb_sz:req, src_ptr:req, src_sz:req, tmp:req, xmm:req, s1:req, s2:req, s3:req
	sub \rb_head, \rb_sz
	cmp \rb_sz, \src_sz
    cmovb \src_sz, \rb_sz

	lea (\rb_ptr, \rb_head), \rb_head

	mov \rb_ptr, \s1
	mov \src_ptr, \s2
	mov \rb_sz, \s3
	memcpy \rb_head, \src_ptr, \rb_sz, \tmp, \xmm
	mov \s1, \rb_ptr
	mov \s2, \src_ptr
	mov \s3, \rb_sz

	add \rb_sz, \src_ptr
	sub \rb_sz, \src_sz

	mov \rb_ptr, \s1
	mov \src_ptr, \s2
	mov \rb_sz, \s3
	memcpy \rb_ptr, \src_ptr, \src_sz, \tmp, \xmm
	mov \s1, \rb_ptr
.endm
)");

	NO_SANITIZE(address) NO_SANITIZE(thread) NO_SANITIZE(undefined)
	auto MPSCPCQueueAny::TryPush(const void* elem, size_type elemsize) noexcept -> bool
	{
		static thread_local const auto* cs = create_crit_section();
		const auto percpu_queue_size = m_percpu_queue_size;
		const auto per_cpu_ring_buf_size = m_per_cpu_ring_buf_size;
		auto res = false;
		auto* self = this;

		unsigned cpu_start;
		size_type* head_ptr;
		size_type head;
		size_type tail;
		SPSCQueueAny* queue;
		size_type newhead;

		// Restartable Sequences: https://github.com/torvalds/linux/blob/master/kernel/rseq.c#L26

		// NOLINTNEXTLINE
		asm volatile(
			// clang-format off

		DEFINE_LABEL(critical_section_retry)

			// Arm Critcal Section.
			R"(
				mov %[cs], %%rdx
				mov %[rseq_cpu_start], %%eax
				mov %%rdx, %[rseq_cs]
				mov %%eax, %[cpu]
			)"
			// Effect: cpu (eax) = RSEQ().cpu_id_start


		DEFINE_LABEL(critical_section_start)

			// Load Per-CPU Queue
			R"(
				mov %[this_], %%rdi
				mov %[mpsc_sz], %%rcx
				mov %[percpu_queue_size], %%r8
				get_queue %%rdi, %%rcx, %%r8
				mov %%rdi, %[queue]
			)"
			// Effect: queue(rdi) = get_queue(*this, cpu)


			// Load Head Ptr, Head and Tail.
			// Compute new head
			R"(
				mov %[head_off], %%rcx
				mov %[tail_off], %%r8
				load_head_ptr_and_tail %%rdi, %%rcx, %%r8, %%r9, %%r10, %%r11
				mov %%r9, %[head_ptr]
				mov %%r10, %[head]
				mov %%r11, %[tail]

				mov %[elemsize], %%r9
				mov %[qword_sz], %%rcx
				compute_newhead %%r10, %%r9, %%rcx
				mov %%r10, %[newhead]
			)"
			// Effect:
			// head_ptr = &queue.m_head.val;
			// head = *head_ptr;
			// tail(r11) = *queue.m_tail.val;
			// newhead(r10) = head + elemsize + sizeof(size_t)


			// Return if Queue is full.
			// is_full = newhead - tail > queue.m_queue_size;
			R"(
				// newqueue_sz(%%r10) = newhead - tail
				sub %%r11, %%r10

				// per_cpu_ring_buf_size(%%r9) 
				mov %[per_cpu_ring_buf_size], %%r9

				cmp %%r9, %%r10
				ja ret%=
			)"
			// Effect: Exit if queue is full.


			// Load Queue Data Ptr
			R"(
				mov %[queue], %%rcx
				mov %[spsc_qz], %%rdx
				mov %[spsc_align], %%rax
				get_queue_data %%rcx, %%rdx, %%rax
			)"
			// Effect queue_data(rcx) = get_queue_data(queue)


			// Copy Element to Ring Buffer
			R"(
				mov %[head], %%rax
				xor %%rdx, %%rdx
				divq %[per_cpu_ring_buf_size]

				// r8 = `elemsize` start pos
				mov %%rdx, %%r8
				mov %%rdx, %%rax
				add %[qword_sz], %%rax
				xor %%rdx, %%rdx
				divq %[per_cpu_ring_buf_size]
				// r9 = `elem` start pos
				mov %%rdx, %%r9

				mov %[per_cpu_ring_buf_size], %%rax
				lea %[elemsize], %%rdi
				mov %[qword_sz], %%rsi
				copy_to_ring_buf %%rcx, %%r8, %%rax, %%rdi, %%rsi, %%r11, %%xmm0, %%rdx, %%r10, %%r12

				mov %[per_cpu_ring_buf_size], %%rax
				mov %[elem], %%rdi
				mov %[elemsize], %%rsi
				copy_to_ring_buf %%rcx, %%r9, %%rax, %%rdi, %%rsi, %%r11, %%xmm0, %%rdx, %%r10, %%r12
			)"
			// Effect: `elem` copied into the queue.


			// Commit
			R"(
				mov %[newhead], %%rcx
				mov %[head_ptr], %%rax
				mov %[cpu], %%edx
				// Commit
				cmp %[cpu_now], %%edx
				jne )" LABEL(critical_section_abort) R"(
				mov %%rcx, (%%rax)
			)"
			// Effect: Ends Critical Section. *head_ptr = newhead


		DEFINE_LABEL(critical_section_postcommit)
			R"(
				movb $1, %[res]
			ret%=:
			)"
			// Effect: res = true

			// clang-format on

			: [res] "+m"(res), [rseq_cs] "=m"(RestartableSequence::GetRseqCS()),
			[cpu] "=&m"(cpu_start), [queue] "=&m"(queue), [head_ptr] "=&m"(head_ptr),
			[tail] "=&m"(tail), [head] "=&m"(head), [newhead] "=&m"(newhead)
			: [this_] "m"(self), [elem] "m"(elem), [elemsize] "m"(elemsize), [cs] "m"(cs),
			[mpsc_sz] "i"(sizeof(MPSCPCQueueAny)), [percpu_queue_size] "m"(percpu_queue_size),
			[per_cpu_ring_buf_size] "m"(per_cpu_ring_buf_size),
			[rseq_cpu_start] "m"(RestartableSequence::GetRseqCpuIdStart()),
			[cpu_now] "m"(RestartableSequence::GetRseqCpuId()),
			[head_off] "i"(offsetof(SPSCQueueAny, m_head)),
			[tail_off] "i"(offsetof(SPSCQueueAny, m_tail)), [qword_sz] "i"(sizeof(size_type)),
			[spsc_qz] "i"(sizeof(SPSCQueueAny)), [spsc_align] "i"(alignof(SPSCQueueAny))
			: "cc", "memory", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12",
			"xmm0");

		return res;
	}

	auto MPSCPCQueueAny::Available() noexcept -> bool { return RestartableSequence::Available(); }

	auto MPSCPCQueueAny::IsFull() const noexcept -> bool
	{
		return is_full(static_cast<int>(RestartableSequence::CurrentCpu()));
	}

	auto MPSCPCQueueAny::IsEmpty() const noexcept -> bool
	{
		for (int i = 0; i < NUM_CORES; i++)
		{
			if (!is_empty(i))
				return false;
		}
		return true;
	}

	// NOLINTNEXTLINE
	DEFINE_CRITICAL_SECTION(NAME(cs), LABEL(critical_section_start),
		LABEL(critical_section_postcommit), LABEL(critical_section_abort));

	// NOLINTNEXTLINE
	DEFINE_ABORT_BLOCK(STR(RSEQ_SIG), critical_section_retry, critical_section_abort);
}
