#include "bamliquidator.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

#include <hdf5.h>
#include <hdf5_hl.h>

#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>
#include <tbb/task_scheduler_init.h>

// this CountH5Record must match exactly the structure in HDF5
// -- see bamliquidator_batch.py function create_count_table
struct CountH5Record
{
  uint32_t bin_number;
  char cell_type[16];
  char chromosome[16];
  uint64_t count;
  char file_name[64];
};

std::vector<CountH5Record> count(const std::string chr,
                                 const std::string cell_type,
                                 const unsigned int bin_size,
                                 const size_t length,
                                 const unsigned int extension,
                                 const char strand,
                                 const std::string bam_file)
{
  const std::string bam_file_name = file_name_from_path(bam_file);

  int bins = std::ceil(length / (double) bin_size);
  int max_base_pair = bins * bin_size;

  const std::vector<double> bin_counts = liquidate(bam_file, chr, 0, max_base_pair, strand, bins, extension);

  CountH5Record record;
  record.bin_number = 0;
  strncpy(record.cell_type,  cell_type.c_str(),     sizeof(CountH5Record::cell_type));
  strncpy(record.chromosome, chr.c_str(),           sizeof(CountH5Record::chromosome));
  strncpy(record.file_name,  bam_file_name.c_str(), sizeof(CountH5Record::file_name));

  std::vector<CountH5Record> records(bin_counts.size(), record);
  for (size_t bin=0; bin < bin_counts.size(); ++bin)
  {
    records[bin].bin_number = bin;
    records[bin].count = bin_counts[bin];
  }

  return records;
}

void write(hid_t& file,
           const std::vector<CountH5Record>& records)
{
  const size_t record_size = sizeof(CountH5Record);

  size_t record_offset[] = { HOFFSET(CountH5Record, bin_number),
                             HOFFSET(CountH5Record, cell_type),
                             HOFFSET(CountH5Record, chromosome),
                             HOFFSET(CountH5Record, count),
                             HOFFSET(CountH5Record, file_name) };

  size_t field_sizes[] = { sizeof(CountH5Record::bin_number),
                           sizeof(CountH5Record::cell_type),
                           sizeof(CountH5Record::chromosome),
                           sizeof(CountH5Record::count),
                           sizeof(CountH5Record::file_name) };

  herr_t status = H5TBappend_records(file, "bin_counts", records.size(), record_size,
                                     record_offset, field_sizes, records.data());
  if (status != 0)
  {
    std::cerr << "Error appending record, status = " << status << std::endl;
  }
}

class Liquidator 
{
public:
  Liquidator(const std::string& bam_file_path):
    bam_file_path(bam_file_path),
    fp(nullptr),
    bamidx(nullptr)
  {
    init();
  }

  Liquidator(const Liquidator& other):
    bam_file_path(other.bam_file_path),
    fp(nullptr),
    bamidx(nullptr)
  {
    init();
  }

  Liquidator& operator=(const Liquidator& other) = delete;

  ~Liquidator()
  {
    bam_index_destroy(bamidx);
    samclose(fp);
  }

  double liquidate(const std::string& chromosome, int start, int stop, char strand, unsigned int extension)
  {
    std::vector<double> counts = ::liquidate(fp, bamidx, chromosome, start, stop, strand, 1, extension);
    if (counts.size() != 1)
    {
      throw std::runtime_error("liquidate failed to provide exactly one count (count is " +
        boost::lexical_cast<std::string>(counts.size()) + ")");
    }
    return counts[0];
  }

private:
  std::string bam_file_path;
  samfile_t* fp;
  bam_index_t* bamidx;

  void init()
  {
    fp = samopen(bam_file_path.c_str(),"rb",0);
    if(fp == NULL)
    {
      throw std::runtime_error("samopen() error with " + bam_file_path);
    }

    bamidx = bam_index_load(bam_file_path.c_str());
    if (bamidx == NULL)
    {
      throw std::runtime_error("bam_index_load() error with " + bam_file_path);
    }
  }
};

// my testing doesn't show using ets keys significantly improving performance,
// but it doesn't hurt and I guess might help with the right hardware
typedef tbb::enumerable_thread_specific<Liquidator,
                                        tbb::cache_aligned_allocator<Liquidator>,
                                        tbb::ets_key_per_instance>
        Liquidators;


void liquidate_bins(std::vector<CountH5Record>& counts, const std::string& bam_file_path,
                    size_t region_begin, size_t region_end, const size_t bin_size,
                    unsigned int extension, const char strand,
                    Liquidators& liquidators)
{
  Liquidator& liquidator = liquidators.local();

  for (size_t i=region_begin; i < region_end; ++i)
  {
    try
    {
      const size_t start = counts[i].bin_number * bin_size;
      const size_t stop = start + bin_size;
      counts[i].count = liquidator.liquidate(counts[i].chromosome,
                                              start, 
                                              stop, 
                                              strand,
                                              extension);
    } catch(const std::exception& e)
    {
      std::cerr << "Skipping " << counts[i].chromosome << " bin " << i << " due to error: "
                << e.what() << std::endl;
    }
  }
}

void batch_liquidate(std::vector<CountH5Record>& counts,
                     const unsigned int bin_size,
                     const unsigned int extension,
                     const char strand,
                     const std::string& bam_file_path)
{
  Liquidators liquidators((Liquidator(bam_file_path))); 

  tbb::parallel_for(
    tbb::blocked_range<int>(0, counts.size(), 1),
    [&](const tbb::blocked_range<int>& range)
    {
      liquidate_bins(counts, bam_file_path, range.begin(), range.end(), bin_size, extension, strand, liquidators);
    },
    tbb::auto_partitioner());
}

std::vector<CountH5Record> count_placeholders(
  const std::vector<std::pair<std::string, size_t>>& chromosome_lengths,
  const std::string& cell_type,
  const std::string& bam_file_path,
  const unsigned int bin_size)
{
  size_t num_records = 0;
  for (auto& chr_length : chromosome_lengths)
  {
    int bins = std::ceil(chr_length.second / (double) bin_size);
    num_records += bins; 
  }
  const std::string bam_file_name = file_name_from_path(bam_file_path);

  CountH5Record empty_record;
  empty_record.bin_number = 0;
  empty_record.count      = 0;
  strncpy(empty_record.cell_type,  cell_type.c_str(),     sizeof(CountH5Record::cell_type));
  strncpy(empty_record.chromosome, "",                    sizeof(CountH5Record::chromosome));
  strncpy(empty_record.file_name,  bam_file_name.c_str(), sizeof(CountH5Record::file_name));

  std::vector<CountH5Record> records(num_records, empty_record);

  size_t i=0;
  for (auto& chr_length : chromosome_lengths)
  {
    int bins = std::ceil(chr_length.second / (double) bin_size);
    for (size_t j=0; j < bins; ++j, ++i)
    {
      records[i].bin_number = j;
      strncpy(records[i].chromosome, chr_length.first.c_str(), sizeof(CountH5Record::chromosome));
    }
  }

  return records;
}

int main(int argc, char* argv[])
{
  tbb::task_scheduler_init init; 

  try
  {
    if (argc <= 9 || argc % 2 != 1)
    {
      std::cerr << "usage: " << argv[0] << " cell_type bin_size extension strand bam_file hdf5_file chr1 length1 ... \n"
        << "\ne.g. " << argv[0] << " mm1s 100000 0 . /ifs/hg18/mm1s/04032013_D1L57ACXX_4.TTAGGC.hg18.bwt.sorted.bam "
        << "chr1 247249719 chr2 242951149 chr3 199501827"
        << "\nnote that this application is intended to be run from bamliquidator_batch.py -- see"
        << "\nhttps://github.com/BradnerLab/pipeline/wiki for more information"
        << std::endl;
      return 1;
    }

    const std::string cell_type = argv[1];
    const unsigned int bin_size = boost::lexical_cast<unsigned int>(argv[2]);
    const unsigned int extension = boost::lexical_cast<unsigned int>(argv[3]);
    const char strand = boost::lexical_cast<char>(argv[4]);
    const std::string bam_file_path = argv[5];
    const std::string hdf5_file_path = argv[6];

    std::vector<std::pair<std::string, size_t>> chromosome_lengths;
    for (int arg = 7; arg < argc && arg + 1 < argc; arg += 2)
    {
      chromosome_lengths.push_back(
        std::make_pair(argv[arg], boost::lexical_cast<size_t>(argv[arg+1])));
    }

    if (bin_size == 0)
    {
      std::cerr << "bin size cannot be zero" << std::endl;
      return 2;
    }

    hid_t h5file = H5Fopen(hdf5_file_path.c_str(), H5F_ACC_RDWR, H5P_DEFAULT);
    if (h5file < 0)
    {
      std::cerr << "Failed to open H5 file " << hdf5_file_path << std::endl;
      return 3;
    }

    std::vector<CountH5Record> counts = count_placeholders(chromosome_lengths, cell_type, bam_file_path, bin_size);
    batch_liquidate(counts, bin_size, extension, strand, bam_file_path);
    write(h5file, counts);

    H5Fclose(h5file);

    return 0;
  }
  catch(const std::exception& e)
  {
    std::cerr << "Unhandled exception: " << e.what() << std::endl;

    return 4; 
  }
}

/* The MIT License (MIT) 

   Copyright (c) 2013 John DiMatteo (jdimatteo@gmail.com)

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE. 
 */