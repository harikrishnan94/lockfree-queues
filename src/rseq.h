#pragma once

#include <boost/config.hpp>
#include <cinttypes>
#include <linux/rseq.h>
#include <type_traits>

/*
 * RSEQ_SIG is used with the following reserved undefined instructions, which
 * trap in user-space:
 *
 * x86-32:    0f b9 3d 5b 8c 62 f6      ud1    0x5b8c62f6,%edi
 * x86-64:    0f b9 3d 5b 8c 62 f6      ud1    0x5b8c62f6(%rip),%edi
 */
#define RSEQ_SIG 0x5b8c62f6


#define NAME(x) "lockfree__MPSCPCQueueAny__TryPush__" #x
#define LABEL(x) NAME(x)
#define DEFINE_LABEL(x) LABEL(x) ":\n\t"
#define STR_1(x) #x
#define STR(x) STR_1(x)
#define READ_ONCE(x)                                                                               \
	((typename std::add_const_t<std::add_volatile_t<std::decay_t<decltype((x))>>>&)((x)))

// NOLINTNEXTLINE
#define DEFINE_ABORT_BLOCK(signature, start_label, abort_label)                                    \
	asm(".pushsection __rseq_failure, \"ax\"\n\t"                                                  \
                                                                                                   \
		".byte 0x0f, 0xb9, 0x3d\n\t"                                                               \
		".long " signature "\n\t"                                                                  \
                                                                                                   \
		DEFINE_LABEL(abort_label)                                                                  \
                                                                                                   \
			"jmp " LABEL(start_label) "\n\t"                                                       \
									  ".popsection\n\t")

// NOLINTNEXTLINE
#define DEFINE_CRITICAL_SECTION(LABEL, start_label, postcommit_label, abort_label)                 \
	asm(".pushsection __rseq_cs, \"aw\"\n\t"                                                       \
		".balign 32\n\t"                                                                           \
                                                                                                   \
		LABEL ":"                                                                                  \
		".long "                                                                                   \
		"0x0"                                                                                      \
		", "                                                                                       \
		"0x0"                                                                                      \
		"\n\t"                                                                                     \
		".quad " start_label ", "                                                                  \
		"(" postcommit_label " - " start_label ")"                                                 \
		", " abort_label "\n\t"                                                                    \
		".popsection\n\t"                                                                          \
		".pushsection __rseq_cs_ptr_array, \"aw\"\n\t"                                             \
		".quad " LABEL "\n\t"                                                                      \
		".popsection\n\t"                                                                          \
		".pushsection __rseq_exit_point_array, \"aw\"\n\t"                                         \
		".quad " start_label ", " abort_label "\n\t"                                               \
		".popsection\n\t")

// NOLINTNEXTLINE(hicpp-special-member-functions,cppcoreguidelines-special-member-functions)
class RestartableSequence
{
public:
	RestartableSequence() { register_current_thread(); }

	~RestartableSequence() noexcept { unregister_current_thread(); }

	static auto Available() noexcept -> bool;

	static auto CurrentCpu() noexcept -> uint32_t
	{
		if (auto cpu = int(READ_ONCE(rseq_abi.cpu_id)); BOOST_LIKELY(cpu >= 0))
			return cpu;
		return current_cpu_fallback();
	}

	// GCC-11 static analysis flags a bogus error, when defined inline
	static auto GetRseqCS() noexcept -> rseq_cs&;

	static auto GetRseqCpuIdStart() noexcept -> uint32_t& { return rseq_abi.cpu_id_start; }
	static auto GetRseqCpuId() noexcept -> uint32_t& { return rseq_abi.cpu_id; }

private:
	static thread_local rseq rseq_abi; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

	// Register rseq for the current thread
	static void register_current_thread();

	// Unregister rseq for current thread.
	static void unregister_current_thread() noexcept;

	static auto current_cpu_fallback() noexcept -> uint32_t;
};
