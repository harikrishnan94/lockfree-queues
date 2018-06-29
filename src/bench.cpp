#include <iostream>
#include <string>

extern int mpmc_bench_main(int argc, char *argv[]);
extern int mpsc_bench_main(int argc, char *argv[]);

int
main(int argc, char *argv[])
{
	if (argc == 1)
	{
		std::cerr << "Usage: [bench] args...\n";
		return -1;
	}

	if (std::string(argv[1]) == "mpmc")
		return mpmc_bench_main(argc, argv);

	if (std::string(argv[1]) == "mpsc")
		return mpsc_bench_main(argc, argv);

	std::cerr << "Valid benchmarks are mpmc and mpsc\n";

	return -1;
}
