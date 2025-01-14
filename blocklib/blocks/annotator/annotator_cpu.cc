/* -*- c++ -*- */
/*
 * Copyright 2010,2013 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "annotator_cpu.hh"
#include <pmt/pmt.h>
#include <string.h>
#include <iomanip>
#include <iostream>

namespace gr {
namespace blocks {

annotator::sptr
annotator::make_cpu(const block_args& args)
{
    return std::make_shared<annotator_cpu>(args);
}

annotator_cpu::annotator_cpu(const block_args& args)
    : annotator(args),
      d_when(args.when),
      d_num_inputs(args.num_inputs),
      d_num_outputs(args.num_outputs),
      d_tpp(args.tpp)
{

    set_tag_propagation_policy(args.tpp);

    d_tag_counter = 0;
    // set_relative_rate(1, 1);
}

work_return_code_t annotator_cpu::work(std::vector<block_work_input>& work_input,
                                   std::vector<block_work_output>& work_output)
{
    // auto in = (const float*)work_input[0].items();
    // auto out = (float*)work_output[0].items();

    auto noutput_items = work_output[0].n_items;

    uint64_t abs_N = 0;

    for (unsigned i = 0; i < d_num_inputs; i++) {
        abs_N = work_input[i].nitems_read();

        auto tags = work_input[i].buffer->tags_in_window(0,noutput_items);
        d_stored_tags.insert(
            d_stored_tags.end(), tags.begin(), tags.end());
    }

    // Storing the current noutput_items as the value to the "noutput_items" key
    auto srcid = pmt::string_to_symbol(alias());
    auto key = pmt::string_to_symbol("seq");

    // Work does nothing to the data stream; just copy all inputs to outputs
    // Adds a new tag when the number of items read is a multiple of d_when
    abs_N = work_output[0].buffer->total_written();

    for (int j = 0; j < noutput_items; j++) {
        // the min() is a hack to make sure this doesn't segfault if
        // there are a different number of ins and outs. This is
        // specifically designed to test the 1-to-1 propagation policy.
        //for (unsigned i = 0; i < std::min(d_num_outputs, d_num_inputs); i++) {
        for (unsigned i = 0; i < d_num_outputs; i++) {
            if (abs_N % d_when == 0) {
                auto value = pmt::from_uint64(d_tag_counter++);
                work_output[i].buffer->add_tag(abs_N, key, value, srcid);
            }

            // We don't really care about the data here
            
            // in = (const float*)work_input[i].items();
            // out = (float*)work_output[i].items();
            // out[j] = in[j];
        }
        abs_N++;
    }
    for (unsigned i = 0; i < d_num_outputs; i++) {
        work_output[i].n_produced = noutput_items;
    }

    return work_return_code_t::WORK_OK;
}

} /* namespace blocks */
} /* namespace gr */
