// Boost.MapReduce library
//
//  Copyright (C) 2009 Craig Henderson.
//  cdm.henderson@googlemail.com
//
//  Use, modification and distribution is subject to the
//  Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// For more information, see http://www.boost.org/libs/mapreduce/
//
 
#ifndef MAPREDUCE_LOCAL_DISK_INTERMEDIATES_HPP
#define MAPREDUCE_LOCAL_DISK_INTERMEDIATES_HPP

#include <iomanip>      // setw
#ifdef __GNUC__
#include <iostream>     // ubuntu linux
#include <fstream>      // ubuntu linux
#endif

namespace mapreduce {

struct null_combiner;

namespace detail {

struct file_lines_comp
{
    template<typename T>
    bool const operator()(T const &first, T const &second)
    {
        return first.second < second.second;
    }
};

template<typename Record>
struct file_merger
{
    template<typename List>
    void operator()(List const &filenames, std::string const &dest)
    {
        std::ofstream outfile(dest.c_str(), std::ios_base::out | std::ios_base::binary);

        std::list<std::string> files, delete_files;
        std::copy(filenames.begin(), filenames.end(), std::back_inserter(files));
        std::copy(filenames.begin(), filenames.end(), std::back_inserter(delete_files));

        while (files.size() > 0)
        {
            typedef std::list<std::pair<boost::shared_ptr<std::ifstream>, Record> > file_lines_t;
            file_lines_t file_lines;
            while (files.size() > 0)
            {
                boost::shared_ptr<std::ifstream> file(new std::ifstream(files.front().c_str(), std::ios_base::in | std::ios_base::binary));
                if (!file->is_open())
                    break;

                files.pop_front();

                std::string line;
                std::getline(*file, line, '\r');

                Record record;
                std::istringstream l(line);
                l >> record;
                file_lines.push_back(std::make_pair(file, record));
            }

            while (file_lines.size() > 0)
            {
                typename file_lines_t::iterator it;
                if (file_lines.size() == 1)
                    it = file_lines.begin();
                else
                    it = std::min_element(file_lines.begin(), file_lines.end(), file_lines_comp());

                Record record = it->second;
                while (it != file_lines.end())
                {
                    if (it->second == record)
                    {
                        outfile << it->second << "\r";

                        std::string line;
                        std::getline(*it->first, line, '\r');

                        if (line.length() > 0)
                        {
                            std::istringstream l(line);
                            l >> it->second;
                        }

                        if (it->first->eof())
                        {
                            typename file_lines_t::iterator it1 = it++;
                            file_lines.erase(it1);
                        }
                        else
                            ++it;
                    }
                    else
                        ++it;
                }
            }

            // subsequent times around the loop need to merge with outfilename
            // from previous iteration. to do this, rename the output file to
            // a temporary filename and add it to the list of files to merge.
            // then re-open the destination file
            if (files.size() > 0)
            {
                outfile.close();

                std::string const temp_filename = platform::get_temporary_filename();
                delete_file(temp_filename);
                boost::filesystem::rename(dest, temp_filename);
                delete_files.push_back(temp_filename);

                files.push_back(temp_filename);
                outfile.open(dest.c_str(), std::ios_base::out | std::ios_base::binary);
            }
        }

        // delete fragment files
        std::for_each(
            delete_files.begin(),
            delete_files.end(),
            boost::bind(detail::delete_file, _1));
    }
};

template<typename Record>
struct file_sorter
{
    bool const operator()(char const *in, char const *out) const
    {
        return mapreduce::merge_sort<Record>(in, out);
    }
};

}   // namespace detail

namespace intermediates {

template<typename MapTask, typename ReduceTask>
class reduce_file_output
{
  public:
    reduce_file_output(std::string const &output_filespec,
                       unsigned    const  partition,
                       unsigned    const  num_partitions)
    {
        std::ostringstream filename;
        filename << output_filespec << partition+1 << "_of_" << num_partitions;
        filename_ = filename.str();
        output_file_.open(filename_.c_str(), std::ios_base::binary);
    }

    void operator()(typename ReduceTask::key_type   const &key,
                    typename ReduceTask::value_type const &value)
    {
        output_file_ << key << "\t" << value << "\r";
    }

  private:
    std::string   filename_;
    std::ofstream output_file_;
};


template<
    typename MapTask,
    typename ReduceTask,
    typename PartitionFn=mapreduce::hash_partitioner,
    typename SortFn=mapreduce::detail::file_sorter<std::pair<typename ReduceTask::key_type, typename ReduceTask::value_type> >,
    typename MergeFn=mapreduce::detail::file_merger<std::pair<typename ReduceTask::key_type, typename ReduceTask::value_type> > >
class local_disk : boost::noncopyable
{
  private:
    struct intermediate_file_info
    {
        intermediate_file_info()
        {
        }

        intermediate_file_info(std::string const &fname)
          : filename(fname)
        {
        }

        struct kv_file : public std::ofstream
        {
            kv_file() : sorted_(true)
            {
            }

            ~kv_file()
            {
                close();
            }

            void open(std::string const &filename)
            {
                assert(records_.empty());
                use_cache_ = true;
                std::ofstream::open(filename.c_str(), std::ios_base::binary);
            }

            void close(void)
            {
                if (is_open())
                {
                    flush_cache();
                    std::ofstream::close();
                }
            }

            bool const sorted(void) const
            {
                return sorted_;
            }

            bool const write(typename ReduceTask::key_type   const &key,
                             typename ReduceTask::value_type const &value)
            {
                if (use_cache_)
                {
                    ++records_.insert(std::make_pair(std::make_pair(key,value),0U)).first->second;
                    return true;
                }

                sorted_ = false;
                return write(key, value, 1);
            }

          protected:
            bool const write(typename ReduceTask::key_type   const &key,
                             typename ReduceTask::value_type const &value,
                             unsigned                        const count)
            {
                std::ostringstream linebuf;
                linebuf << key.length() << "\t" << key << "\t" << value << "\r";

                std::string line(linebuf.str());
                for (unsigned loop=0; loop<count; ++loop)
                {
                    *this << line;
                    if (bad()  ||  fail())
                        return false;
                }
                return true;
            }

            bool const flush_cache(void)
            {
                use_cache_ = false;
                for (records_t::const_iterator it  = records_.begin(); it != records_.end(); ++it)
                {
                    if (!write(it->first.first, it->first.second, it->second))
                        return false;
                }

                records_.clear();
                return true;
            }

          private:
            typedef
            std::pair<typename ReduceTask::key_type,
                      typename ReduceTask::value_type>
            record_t;

            typedef
            std::map<record_t, unsigned>
            records_t;

            bool      sorted_;
            bool      use_cache_;
            records_t records_;
        };

        std::string             filename;
        kv_file                 write_stream;
        std::list<std::string>  fragment_filenames;
    };

    typedef
    std::map<
        size_t, // hash value of intermediate key (R)
        boost::shared_ptr<intermediate_file_info> >
    intermediates_t;

  public:
    typedef MapTask    map_task_type;
    typedef ReduceTask reduce_task_type;

    typedef
    reduce_file_output<MapTask, ReduceTask>
    store_result_type;

    typedef
    std::pair<
        typename reduce_task_type::key_type,
        typename reduce_task_type::value_type>
    keyvalue_t;

    class const_result_iterator
      : public boost::iterator_facade<
            const_result_iterator,
            keyvalue_t const,
            boost::forward_traversal_tag>
    {
        friend class boost::iterator_core_access;

      protected:
        explicit const_result_iterator(local_disk const *outer)
          : outer_(outer)
        {
            BOOST_ASSERT(outer_);
            kvlist_.resize(outer_->num_partitions_);
        }

        void increment(void)
        {
            if (!kvlist_[index_].first->eof())
                read_record(*kvlist_[index_].first, kvlist_[index_].second.first, kvlist_[index_].second.second);
            set_current();
        }

        bool const equal(const_result_iterator const &other) const
        {
            return (kvlist_.size() == 0  &&  other.kvlist_.size() == 0)
               ||  (kvlist_.size() > 0
               &&  other.kvlist_.size() > 0
               &&  kvlist_[index_].second == other.kvlist_[index_].second);
        }

        const_result_iterator &begin(void)
        {
            for (unsigned loop=0; loop<outer_->num_partitions_; ++loop)
            {
                kvlist_[loop] =
                    std::make_pair(
                        boost::shared_ptr<std::ifstream>(
                            new std::ifstream),
                            keyvalue_t());

                kvlist_[loop].first->open(
                    outer_->intermediate_files_.find(loop)->second->filename.c_str(),
                    std::ios_base::binary);

                BOOST_ASSERT(kvlist_[loop].first->is_open());
                read_record(
                    *kvlist_[loop].first,
                    kvlist_[loop].second.first,
                    kvlist_[loop].second.second);
            }
            set_current();
            return *this;
        }

        const_result_iterator &end(void)
        {
            index_ = 0;
            kvlist_.clear();
            return *this;
        }

        keyvalue_t const &dereference(void) const
        {
            return kvlist_[index_].second;
        }

        void set_current(void)
        {
            index_ = 0;
            while (index_<outer_->num_partitions_  &&  kvlist_[index_].first->eof())
                 ++index_;
            
            for (unsigned loop=index_+1; loop<outer_->num_partitions_; ++loop)
            {
                if (!kvlist_[loop].first->eof()  &&  !kvlist_[index_].first->eof()  &&  kvlist_[index_].second > kvlist_[loop].second)
                    index_ = loop;
            }

            if (index_ == outer_->num_partitions_)
                end();
        }

      private:
        local_disk                    const *outer_;        // parent container
        unsigned                             index_;        // index of current element
        typedef
        std::vector<
            std::pair<
                boost::shared_ptr<std::ifstream>,
                keyvalue_t> >
        kvlist_t;
        kvlist_t kvlist_;

        friend class local_disk;
    };
    friend class const_result_iterator;

    local_disk(unsigned const num_partitions)
      : num_partitions_(num_partitions)
    {
    }

    ~local_disk()
    {
        try
        {
            this->close_files();

            // delete the temporary files
            for (intermediates_t::iterator it=intermediate_files_.begin();
                 it!=intermediate_files_.end();
                 ++it)
            {
                detail::delete_file(it->second->filename);
                std::for_each(
                    it->second->fragment_filenames.begin(),
                    it->second->fragment_filenames.end(),
                    boost::bind(detail::delete_file, _1));
            }
        }
        catch (std::exception const &e)
        {
            std::cerr << "\nError: " << e.what() << "\n";
        }
    }

    const_result_iterator begin_results(void) const
    {
        return const_result_iterator(this).begin();
    }

    const_result_iterator end_results(void) const
    {
        return const_result_iterator(this).end();
    }

    template<typename StoreResult>
    bool const insert(typename reduce_task_type::key_type   const &key,
                      typename reduce_task_type::value_type const &value,
                      StoreResult &store_result)
    {
        store_result(key, value);
        return insert(key, value);
    }

    bool const insert(typename reduce_task_type::key_type   const &key,
                      typename reduce_task_type::value_type const &value)
    {
        unsigned const partition = partitioner_(key, num_partitions_);

        intermediates_t::iterator it = intermediate_files_.find(partition);
        if (it == intermediate_files_.end())
        {
            it = intermediate_files_.insert(
                    std::make_pair(
                        partition,
                        boost::shared_ptr<intermediate_file_info>(
                            new intermediate_file_info))).first;
        }

        if (it->second->filename.empty())
        {
            it->second->filename = platform::get_temporary_filename();
            assert(!it->second->write_stream.is_open());
        }

        if (!it->second->write_stream.is_open())
            it->second->write_stream.open(it->second->filename);
        assert(it->second->write_stream.is_open());
        return it->second->write_stream.write(key, value);
    }

    template<typename T>
    size_t const length(T const &value)
    {
        return value.length();
    }

    template<typename FnObj>
    void combine(FnObj &fn_obj)
    {
        this->close_files();
        for (intermediates_t::iterator it=intermediate_files_.begin(); it!=intermediate_files_.end(); ++it)
        {
            std::string infilename  = it->second->filename;
            std::string outfilename = platform::get_temporary_filename();

            // sort the input file
            sort_fn_(infilename.c_str(), outfilename.c_str());
            detail::delete_file(infilename);
            std::swap(infilename, outfilename);

            reduce_task_type::key_type key, last_key;
            typename reduce_task_type::value_type value;
            std::ifstream infile(infilename.c_str());
            while (read_record(infile, key, value))
            {
                if (key != last_key  &&  length(key) > 0)
                {
                    if (length(last_key) > 0)
                        fn_obj.finish(last_key, *this);
                    if (length(key) > 0)
                    {
                        fn_obj.start(key);
                        std::swap(key, last_key);
                    }
                }

                fn_obj(value);
            }

            if (length(last_key) > 0)
                fn_obj.finish(last_key, *this);

            infile.close();

            detail::delete_file(infilename);
        }

        this->close_files();
    }

    void combine(mapreduce::null_combiner &/*fn_obj*/)
    {
        this->close_files();
    }

    void merge_from(local_disk &other)
    {
        BOOST_ASSERT(num_partitions_ == other.num_partitions_);
        for (unsigned partition=0; partition<num_partitions_; ++partition)
        {
            intermediates_t::iterator ito = other.intermediate_files_.find(partition);
            if (ito != other.intermediate_files_.end())
            {
                intermediates_t::iterator it = intermediate_files_.find(partition);
                if (it == intermediate_files_.end())
                {
                    it = intermediate_files_.insert(
                            std::make_pair(
                                partition,
                                boost::shared_ptr<intermediate_file_info>(
                                    new intermediate_file_info))).first;
                }

                ito->second->write_stream.close();
                if (ito->second->write_stream.sorted())
                {
                    it->second->fragment_filenames.push_back(ito->second->filename);
                    ito->second->filename.clear();
                }
                else
                {
                    std::string const sorted = platform::get_temporary_filename();
                    sort_fn_(ito->second->filename.c_str(), sorted.c_str());
                    it->second->fragment_filenames.push_back(sorted);
                }
                assert(ito->second->fragment_filenames.empty());
            }
        }
    }

    void run_intermediate_results_shuffle(unsigned const partition)
    {
/*!!!*/ std::cout << "\nIntermediate Results Shuffle, Partition " << partition << "...";
        intermediates_t::iterator it = intermediate_files_.find(partition);
        assert(it != intermediate_files_.end());
        it->second->write_stream.close();
        if (!it->second->fragment_filenames.empty())
        {
            it->second->filename = platform::get_temporary_filename();
            merge_fn_(it->second->fragment_filenames, it->second->filename);
        }
    }

    template<typename Callback>
    void reduce(unsigned const partition, Callback &callback)
    {
/*!!!*/ std::cout << "\nReduce Phase running for partition " << partition << "...";

        intermediates_t::iterator it = intermediate_files_.find(partition);
        BOOST_ASSERT(it != intermediate_files_.end());

        std::string filename;
        std::swap(filename, it->second->filename);
        it->second->write_stream.close();
        intermediate_files_.erase(it);

        std::pair<
            typename reduce_task_type::key_type,
            typename reduce_task_type::value_type> kv;
        typename reduce_task_type::key_type   last_key;
        std::list<typename reduce_task_type::value_type> values;
        std::ifstream infile(filename.c_str());
        while (!(infile >> kv).eof())
        {
            if (kv.first != last_key  &&  length(kv.first) > 0)
            {
                if (length(last_key) > 0)
                {
                    callback(last_key, values.begin(), values.end());
                    values.clear();
                }
                if (length(kv.first) > 0)
                    std::swap(kv.first, last_key);
            }

            values.push_back(kv.second);
        }

        if (length(last_key) > 0)
            callback(last_key, values.begin(), values.end());

        infile.close();
        detail::delete_file(filename.c_str());
    }

    static bool const read_record(std::istream &infile,
                                  typename reduce_task_type::key_type   &key,
                                  typename reduce_task_type::value_type &value)
    {
#if defined(__SGI_STL_PORT)
        size_t keylen;
#else
        std::streamsize keylen;
#endif
        keylen = 0;
        infile >> keylen;
        if (infile.eof()  ||  keylen == 0)
            return false;

        char tab;
        infile.read(&tab, 1);

        key.resize(keylen);
        infile.read(&*key.begin(), keylen);
        infile.read(&tab, 1);
        infile >> value;
        return true;
    }

  private:
    void close_files(void)
    {
        for (intermediates_t::iterator it=intermediate_files_.begin(); it!=intermediate_files_.end(); ++it)
            it->second->write_stream.close();
    }

  private:
    typedef enum { map_phase, reduce_phase } phase_t;

    unsigned const  num_partitions_;
    intermediates_t intermediate_files_;
    SortFn          sort_fn_;
    MergeFn         merge_fn_;
    PartitionFn     partitioner_;
};

}   // namespace intermediates

}   // namespace mapreduce

#endif  // MAPREDUCE_LOCAL_DISK_INTERMEDIATES_HPP