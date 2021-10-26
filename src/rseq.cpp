#include <cstdio>
#include <sched.h>
#include <syscall.h>
#include <system_error>

#include "rseq.h"

static constexpr uint32_t CPU_ID_UNINITIALIZED = RSEQ_CPU_ID_UNINITIALIZED;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
thread_local rseq RestartableSequence::rseq_abi = {
	.cpu_id_start = CPU_ID_UNINITIALIZED, .cpu_id = CPU_ID_UNINITIALIZED, .rseq_cs = {}, .flags = 0
};

static auto sys_rseq(struct rseq* rseq_abi, uint32_t rseq_len, int flags, uint32_t sig) -> long
{
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
	return syscall(__NR_rseq, rseq_abi, rseq_len, flags, sig);
}


auto RestartableSequence::GetRseqCS() noexcept -> rseq_cs&
{
	return *reinterpret_cast<rseq_cs*>(&rseq_abi.rseq_cs.ptr);
}

auto RestartableSequence::Available() noexcept -> bool
{
	if (auto rc = sys_rseq(nullptr, 0, 0, 0); rc != -1)
	{
		std::fprintf(stderr, "rseq syscall returns unexpected value.\n"); // NOLINT
		abort();
	}

	switch (errno)
	{
	case ENOSYS:
	case EPERM:
		return false;
	case EINVAL:
		return true;
	default:
		std::perror("rseq");
		abort();
	}
}

void RestartableSequence::register_current_thread()
{
	if (rseq_abi.cpu_id != CPU_ID_UNINITIALIZED)
		throw std::runtime_error("current thread is already registered for rseq.");

	if (sys_rseq(&rseq_abi, sizeof(rseq_abi), 0, RSEQ_SIG) != 0)
		throw std::system_error(errno, std::system_category());
}

void RestartableSequence::unregister_current_thread() noexcept
{
	if (sys_rseq(&rseq_abi, sizeof(rseq_abi), RSEQ_FLAG_UNREGISTER, RSEQ_SIG) != 0)
	{
		perror("RestartableSequence::unregister_current_thread()");
		abort();
	}
}

auto RestartableSequence::current_cpu_fallback() noexcept -> uint32_t
{
	if (auto cpu = sched_getcpu(); BOOST_LIKELY(cpu >= 0))
		return cpu;

	perror("sched_getcpu()");
	abort();
}
