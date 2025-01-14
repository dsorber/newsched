/* -*- c++ -*- */
/*
 * Copyright 2004,2009,2010,2012,2018 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#include "stream_to_streams_cpu.hh"
#include <volk/volk.h>

namespace gr {
namespace streamops {


typename stream_to_streams::sptr
stream_to_streams::make_cpu(const block_args& args)
{
    return std::make_shared<stream_to_streams_cpu>(args);
}

stream_to_streams_cpu::stream_to_streams_cpu(const block_args& args)
    : stream_to_streams(args), d_itemsize(args.itemsize)
{
}

work_return_code_t
stream_to_streams_cpu::work(std::vector<block_work_input>& work_input,
                                       std::vector<block_work_output>& work_output)
{
    auto in = static_cast<const uint8_t*>(work_input[0].items());

    uint8_t * in_ptr = const_cast<uint8_t*>(in);
    auto noutput_items = work_output[0].n_items;
    auto ninput_items = work_input[0].n_items;
    size_t nstreams = work_output.size();

    auto total_items = std::min(ninput_items / nstreams, (size_t)noutput_items);

    for (size_t i = 0; i < total_items; i++) {
        for (size_t j = 0; j < nstreams; j++) {
            memcpy(((uint8_t *)work_output[j].items())+i*d_itemsize, in_ptr, d_itemsize);
            in_ptr += d_itemsize;
        }
    }

    produce_each(total_items, work_output);
    consume_each(total_items*nstreams, work_input);
    return work_return_code_t::WORK_OK;
}


} /* namespace streamops */
} /* namespace gr */
