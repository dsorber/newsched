#include <gnuradio/flowgraph.hpp>

namespace gr {

    void flowgraph::set_scheduler(scheduler_sptr sched)
    {
        d_schedulers = std::vector<scheduler_sptr>{ sched };
    }
    void flowgraph::set_schedulers(std::vector<scheduler_sptr> sched) { d_schedulers = sched; }
    void flowgraph::add_scheduler(scheduler_sptr sched) { d_schedulers.push_back(sched); }
    void flowgraph::clear_schedulers() { d_schedulers.clear(); }
    void flowgraph::partition(std::vector<domain_conf>& confs)
    {
        // Create new subgraphs based on the partition configuration
        d_subgraphs.clear();

        // std::vector<std::tuple<graph,edge>> domain_crossings;
        std::vector<edge> domain_crossings;
        std::vector<domain_conf> crossing_confs;
        std::vector<scheduler_sptr> partition_scheds;

        for (auto& conf : confs) {
            auto g = graph::make(); // create a new subgraph
            // Go through the blocks assigned to this scheduler
            // See whether they connect to the same graph or account for a domain crossing

            auto sched = conf.sched();   // std::get<0>(conf);
            auto blocks = conf.blocks(); // std::get<1>(conf);
            for (auto b : blocks)        // for each of the blocks in the tuple
            {
                for (auto input_port : b->input_stream_ports()) {
                    auto edges = find_edge(input_port);
                    // There should only be one edge connected to an input port
                    // Crossings associated with the downstream port
                    auto e = edges[0];
                    auto other_block = e.src().node();

                    // Is the other block in our current partition
                    if (std::find(blocks.begin(), blocks.end(), other_block) !=
                        blocks.end()) {
                        g->connect(e.src(), e.dst());
                    } else {
                        // add this edge to the list of domain crossings
                        // domain_crossings.push_back(std::make_tuple(g,e));
                        domain_crossings.push_back(e);
                        crossing_confs.push_back(conf);

                        // Is this block connected to anything else in our current
                        // partition
                        bool connected = false; // TODO - handle orphan nodes
                    }
                }
            }

            d_subgraphs.push_back(g);
            partition_scheds.push_back(sched);
        }

        // Now, let's set up domain adapters at the domain crossings
        // Several assumptions are being made now:
        //   1.  All schedulers running on the same processor
        //   2.  Outputs that cross domains can only be mapped one input
        //   3.  Fixed client/server relationship - limited configuration of DA

        int crossing_index = 0;
        for (auto c : domain_crossings) {
            // Attach a domain adapter to the src and dst ports of the edge
            // auto g = std::get<0>(c);
            // auto e = std::get<1>(c);

            // Find the subgraph that holds src block
            graph_sptr src_block_graph = nullptr;
            for (auto g : d_subgraphs) {
                auto blocks = g->calc_used_nodes();
                if (std::find(blocks.begin(), blocks.end(), c.src().node()) !=
                    blocks.end()) {
                    src_block_graph = g;
                    break;
                }
            }

            // Find the subgraph that holds dst block
            graph_sptr dst_block_graph = nullptr;
            for (auto g : d_subgraphs) {
                auto blocks = g->calc_used_nodes();
                if (std::find(blocks.begin(), blocks.end(), c.dst().node()) !=
                    blocks.end()) {
                    dst_block_graph = g;
                    break;
                }
            }

            if (!src_block_graph || !dst_block_graph) {
                throw std::runtime_error("Cannot find both sides of domain adapter");
            }

            // Create Domain Adapter pair
            // right now, only one port - have a list of available ports
            // put the buffer downstream
            auto conf = crossing_confs[crossing_index];

            // Does the crossing have a specific domain adapter defined
            domain_adapter_conf_sptr da_conf = nullptr;
            for (auto ec : conf.da_edge_confs()) {
                auto conf_edge = std::get<0>(ec);
                if (c == conf_edge) {
                    da_conf = std::get<1>(ec);
                    break;
                }
            }


            // else if defined: use the default defined for the domain
            if (!da_conf) {
                da_conf = conf.da_conf();
            }

            // else, use the default domain adapter configuration ??
            // TODO


#if 0
            auto da_src =
                domain_adapter_zmq_req_cli::make(std::string("tcp://127.0.0.1:1234"),
                                                 buffer_preference_t::UPSTREAM,
                                                 c.src().port());
            auto da_dst =
                domain_adapter_zmq_rep_svr::make(std::string("tcp://127.0.0.1:1234"),
                                                 buffer_preference_t::UPSTREAM,
                                                 c.dst().port());
#endif
            // use the conf to produce the domain adapters

            auto da_pair = da_conf->make_domain_adapter_pair(
                c.src().port(),
                c.dst().port(),
                "da_" + c.src().node()->alias() + "->" + c.dst().node()->alias());
            auto da_src = std::get<0>(da_pair);
            auto da_dst = std::get<1>(da_pair);


            // da_src->test();

            // Attach domain adapters to the src and dest blocks
            // domain adapters only have one port
            src_block_graph->connect(c.src(),
                                     node_endpoint(da_src, da_src->all_ports()[0]));
            dst_block_graph->connect(node_endpoint(da_dst, da_dst->all_ports()[0]),
                                     c.dst());


            crossing_index++;
        }

        d_flat_subgraphs.clear();
        for (auto i = 0; i < partition_scheds.size(); i++) {
            d_flat_subgraphs.push_back(flat_graph::make_flat(d_subgraphs[i]));
            partition_scheds[i]->initialize(d_flat_subgraphs[i]);
        }
    }
    void flowgraph::validate()
    {
        gr_log_trace(_debug_logger, "validate()");
        d_flat_graph = flat_graph::make_flat(base());
        for (auto sched : d_schedulers)
            sched->initialize(d_flat_graph);
    }
    void flowgraph::start()
    {
        using namespace std::chrono_literals;
        gr_log_trace(_debug_logger, "start()");
        // Need thread synchronization for the schedulers - to know when they're done and
        // signal the other schedulers that might be connected

        // assign ids to the schedulers
        int idx = 0;
        for (auto s : d_schedulers) {
            s->set_id(idx++);
        }

        // Start a monitor thread to keep track of when the schedulers signal info back to
        // the main thread
        std::thread monitor([this]() {
            while (!_monitor_thread_stopped) {
                std::unique_lock<std::mutex> lk{ _sched_sync.sync_mutex };
                if (_sched_sync.sync_cv.wait_for(
                        lk, 1s, [this] { return _sched_sync.ready == true; })) {
                    gr_log_debug(_debug_logger,
                                 "monitor: notified --> {} / {}",
                                 _sched_sync.id,
                                 (int)_sched_sync.state);

                    if (_sched_sync.state ==
                        scheduler_state::DONE) // Notify the other threads to wrap it up
                    {
                        for (auto s : d_schedulers) {
                            s->set_state(scheduler_state::DONE);
                        }
                        break;
                    }
                } else {
                    continue;
                }
            }

            while (!_monitor_thread_stopped) {
                // Wait until all the threads are done

                bool all_done = true;
                for (auto& s : d_schedulers) {
                    auto state = s->state();
                    // std::cout << "**" << s->name() << ":" << ":state:" << (int)
                    // s->state() << std::endl;;
                    if (state != scheduler_state::FLUSHED) {
                        all_done = false;
                    }
                }
                if (all_done) {
                    for (auto s : d_schedulers) {
                        s->set_state(scheduler_state::EXIT);
                    }
                    break;
                }
            }
        });
        monitor.detach();
        for (auto s : d_schedulers) {
            s->start(&_sched_sync);
        }
    }
    void flowgraph::stop()
    {
        gr_log_trace(_debug_logger, "stop()");
        for (auto s : d_schedulers) {
            s->stop();
        }
    }
    void flowgraph::wait()
    {
        gr_log_trace(_debug_logger, "wait()");
        for (auto s : d_schedulers) {
            s->wait();
        }
    }

} // namespace gr
