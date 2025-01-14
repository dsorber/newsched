#pragma once

#include <gnuradio/blocks/msg_forward.hh>

namespace gr {
namespace blocks {

class msg_forward_cpu : public msg_forward
{
public:
    msg_forward_cpu(block_args args) : msg_forward(args), d_itemsize(args.itemsize) {}
    virtual work_return_code_t work(std::vector<block_work_input>& work_input,
                                    std::vector<block_work_output>& work_output) override;
    virtual size_t message_count() { return d_msg_cnt; }

protected:
    size_t d_itemsize;
    void handle_msg_in(pmt::pmt_t msg)
    {
        gr_log_info(
            _logger, "{} got message: {}", this->alias(), pmt::symbol_to_string(msg));
        d_msg_cnt++;
        get_message_port("out")->post(msg);
    }

private:
    size_t d_msg_cnt = 0;
};

} // namespace blocks
} // namespace gr
