#include <chrono>
#include <iostream>
#include <thread>

#include <gnuradio/blocks/head.hh>
#include <gnuradio/blocks/load.hh>
#include <gnuradio/blocks/null_sink.hh>
#include <gnuradio/blocks/null_source.hh>
#include <gnuradio/blocks/vector_sink.hh>
#include <gnuradio/blocks/vector_source.hh>
#include <gnuradio/flowgraph.hh>
#include <gnuradio/logging.hh>
#include <gnuradio/realtime.hh>
#include <gnuradio/schedulers/nbt/scheduler_nbt.hh>

#include <gnuradio/buffer_cuda.hh>
#include <gnuradio/buffer_cuda_pinned.hh>
#include <gnuradio/buffer_cuda_sm.hh>
#include <gnuradio/buffer_cpu_simple.hh>
#include <gnuradio/buffer_cpu_vmcirc.hh>
#include <iostream>

#include <boost/program_options.hpp>
namespace po = boost::program_options;

using namespace gr;

int main(int argc, char* argv[])
{
    uint64_t samples;
    int mem_model;
    size_t batch_size;
    int nblocks;
    size_t load;
    bool rt_prio = false;
    bool single_mapped = false;

    po::options_description desc("CUDA Copy Benchmarking Flowgraph");
    desc.add_options()("help,h", "display help")(
        "samples,N",
        po::value<uint64_t>(&samples)->default_value(15000000),
        "Number of samples")(
        "nblocks,b", po::value<int>(&nblocks)->default_value(4), "Num FFT Blocks")(
        "load,l", po::value<size_t>(&load)->default_value(1), "Num FFT Blocks")(
        "memmodel,m", po::value<int>(&mem_model)->default_value(0), "Memory Model")(
        "veclen,s", po::value<size_t>(&batch_size)->default_value(1024), "Batch Size")(
        "rt_prio", "Enable Real-time priority")(
        "sm", "Enable Single Mapped CUDA buffers (for memmodel 0)");
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    if (vm.count("rt_prio")) {
        rt_prio = true;
    }

    if (vm.count("sm")) {
        single_mapped = true;
    }

    std::vector<blocks::load::sptr> copy_blks(nblocks);
    for (int i = 0; i < nblocks; i++) {
        copy_blks[i] = blocks::load::make_cuda({ sizeof(gr_complex) * batch_size, load });
    }

    // std::vector<gr_complex> input_data(samples);
    // for (unsigned i = 0; i < samples; i++)
    //     input_data[i] = gr_complex(i % 256, 256 - i % 256);

    auto src = blocks::null_source::make({ sizeof(gr_complex) * batch_size });
    auto snk = blocks::null_sink::make({ sizeof(gr_complex) * batch_size });
    auto head =
        blocks::head::make_cpu({ sizeof(gr_complex) * batch_size, samples / batch_size });

    auto fg = flowgraph::make();

    fg->connect(src, 0, head, 0)->set_custom_buffer(BUFFER_CPU_VMCIRC_ARGS);
    auto sched = schedulers::scheduler_nbt::make(
        "sched",
        sizeof(gr_complex) * batch_size *
            2); // This sizing should be handled in buffer_managment but it is not yet
    sched->set_default_buffer_factory(BUFFER_CPU_VMCIRC_ARGS);
    fg->set_scheduler(sched);
    if (mem_model == 0) {
        if (single_mapped)
            fg->connect(head, 0, copy_blks[0], 0)
                ->set_custom_buffer(CUDA_BUFFER_SM_ARGS_H2D);
        else
            fg->connect(head, 0, copy_blks[0], 0)
                ->set_custom_buffer(CUDA_BUFFER_ARGS_H2D);

        for (int i = 0; i < nblocks - 1; i++) {
            if (single_mapped) {
                fg->connect(copy_blks[i], 0, copy_blks[i + 1], 0)
                    ->set_custom_buffer(CUDA_BUFFER_SM_ARGS_D2D);
            } else {
                fg->connect(copy_blks[i], 0, copy_blks[i + 1], 0)
                    ->set_custom_buffer(CUDA_BUFFER_ARGS_D2D);
            }
        }
        if (single_mapped) {
            fg->connect(copy_blks[nblocks - 1], 0, snk, 0)
                ->set_custom_buffer(CUDA_BUFFER_SM_ARGS_D2H);
        } else {
            fg->connect(copy_blks[nblocks - 1], 0, snk, 0)
                ->set_custom_buffer(CUDA_BUFFER_ARGS_D2H);
        }

    } else {
        fg->connect(head, 0, copy_blks[0], 0)->set_custom_buffer(CUDA_BUFFER_PINNED_ARGS);
        for (int i = 0; i < nblocks - 1; i++) {
            fg->connect(copy_blks[i], 0, copy_blks[i + 1], 0)
                ->set_custom_buffer(CUDA_BUFFER_PINNED_ARGS);
        }
        fg->connect(copy_blks[nblocks - 1], 0, snk, 0)
            ->set_custom_buffer(CUDA_BUFFER_PINNED_ARGS);
    }

    fg->validate();

    if (rt_prio && gr::enable_realtime_scheduling() != gr::rt_status_t::RT_OK)
        std::cout << "Unable to enable realtime scheduling " << std::endl;

    auto t1 = std::chrono::steady_clock::now();
    fg->start();
    fg->wait();

    auto t2 = std::chrono::steady_clock::now();
    auto time =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count() / 1e9;

    std::cout << "[PROFILE_TIME]" << time << "[PROFILE_TIME]" << std::endl;
}
