/* C++ interface wrapper for blob client
 * No exceptions will throw.
 */
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>

#include "blob/blob_client.h"
#include "storage_errno.h"

namespace microsoft_azure {
    namespace storage {

        const unsigned long long DOWNLOAD_CHUNK_SIZE = 16 * 1024 * 1024;

        class mempool
        {
        public:
            ~mempool()
            {
                while(!m_buffers.empty())
                {
                    auto buffer = m_buffers.front();
                    delete[] buffer;
                    m_buffers.pop();
                }
            }

            char* get_buffer()
            {
                std::lock_guard<std::mutex> lg(m_buffers_mutex);
                if(m_buffers.empty())
                {
                    char* buffer = new char[s_block_size];
                    return buffer;
                }
                else
                {
                    char* buffer = m_buffers.front();
                    m_buffers.pop();
                    return buffer;
                }
            }
            void release_buffer(char *buffer)
            {
                std::lock_guard<std::mutex> lg(m_buffers_mutex);
                m_buffers.push(buffer);
            }
        private:
            std::queue<char*> m_buffers;
            std::mutex m_buffers_mutex;
            static const size_t s_block_size = 4*1024*1024;
        };
        static mempool mpool;
        off_t get_file_size(const char* path);

        static const char* _base64_enctbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string to_base64(const char* base, size_t length)
        {
            std::string result;
            for(int offset = 0; length - offset > 3; offset += 3)
            {
                const char* ptr = base + offset;
                unsigned char idx0 = ptr[0] >> 2;
                unsigned char idx1 = ((ptr[0]&0x3)<<4)| ptr[1] >> 4;
                unsigned char idx2 = ((ptr[1]&0xF)<<2)| ptr[2] >> 6;
                unsigned char idx3 = ptr[2]&0x3F;
                result.push_back(_base64_enctbl[idx0]);
                result.push_back(_base64_enctbl[idx1]);
                result.push_back(_base64_enctbl[idx2]);
                result.push_back(_base64_enctbl[idx3]);
            }
            switch(length % 3)
            {
                case 1:
                {

                    const char* ptr = base + length - 1;
                    unsigned char idx0 = ptr[0] >> 2;
                    unsigned char idx1 = ((ptr[0]&0x3)<<4);
                    result.push_back(_base64_enctbl[idx0]);
                    result.push_back(_base64_enctbl[idx1]);
                    result.push_back('=');
                    result.push_back('=');
                    break;
                }
                case 2:
                {

                    const char* ptr = base + length - 2;
                    unsigned char idx0 = ptr[0] >> 2;
                    unsigned char idx1 = ((ptr[0]&0x3)<<4)| ptr[1] >> 4;
                    unsigned char idx2 = ((ptr[1]&0xF)<<2);
                    result.push_back(_base64_enctbl[idx0]);
                    result.push_back(_base64_enctbl[idx1]);
                    result.push_back(_base64_enctbl[idx2]);
                    result.push_back('=');
                    break;
                }
            }
            return result;
        }

        blob_client_wrapper blob_client_wrapper::blob_client_wrapper_init(const std::string &account_name, const std::string &account_key, const unsigned int concurrency)
        {
            return blob_client_wrapper_init(account_name, account_key, concurrency, false);
        }


        blob_client_wrapper blob_client_wrapper::blob_client_wrapper_init(const std::string &account_name, const std::string &account_key, const unsigned int concurrency, const bool use_https)
        {
            if(account_name.length() == 0 || account_key.length() == 0)
            {
                errno = invalid_parameters;
                return blob_client_wrapper(false);
            }

            /* set a default concurrency value. */
            unsigned int concurrency_limit = 40;
            if(concurrency != 0)
            {
                concurrency_limit = concurrency;
            }
            std::string accountName(account_name);
            std::string accountKey(account_key);

            try
            {
                std::shared_ptr<storage_credential>  cred = std::make_shared<shared_key_credential>(accountName, accountKey);
                std::shared_ptr<storage_account> account = std::make_shared<storage_account>(accountName, cred, use_https);
                std::shared_ptr<blob_client> blobClient= std::make_shared<microsoft_azure::storage::blob_client>(account, concurrency_limit);
                errno = 0;
                return blob_client_wrapper(blobClient);
            }
            catch(const std::exception &ex)
            {
                std::cerr << ex.what() << std::endl;
                errno = unknown_error;
                return blob_client_wrapper(false);
            }

        }

        void blob_client_wrapper::create_container(const std::string &container)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return;
            }
            if(container.length() == 0)
            {
                errno = invalid_parameters;
                return;
            }

            try
            {
                auto task = m_blobClient->create_container(container);
                task.wait();
                auto result = task.get();

                if(!result.success())
                {
                    /* container already exists.
                     *               * Bug, need to compare message as well.
                     *                             * */
                    errno = std::stoi(result.error().code);
                }
                else
                {
                    errno = 0;
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return;
            }
        }

        void blob_client_wrapper::delete_container(const std::string &container)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return;
            }
            if(container.length() == 0)
            {
                errno = invalid_parameters;
                return;
            }

            try
            {
                auto task = m_blobClient->delete_container(container);
                task.wait();
                auto result = task.get();

                if(!result.success())
                {
                    errno = std::stoi(result.error().code);
                }
                else
                {
                    errno = 0;
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return;
            }
        }

        bool blob_client_wrapper::container_exists(const std::string &container)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return false;
            }
            if(container.length() == 0)
            {
                errno = invalid_parameters;
                return false;
            }

            try
            {
                auto containerProperty = m_blobClient->get_container_property(container).response();

                if(containerProperty.valid())
                {
                    errno = 0;
                    return true;
                }
                else
                {
                    errno = unknown_error;
                    return false;
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return false;
            }
        }

        std::vector<list_containers_item> blob_client_wrapper::list_containers(const std::string &prefix, bool include_metadata)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return std::vector<list_containers_item>();
            }
            if(prefix.length() == 0)
            {
                errno = invalid_parameters;
                return std::vector<list_containers_item>();
            }

            try
            {
                auto task = m_blobClient->list_containers(prefix, include_metadata);
                task.wait();
                auto result = task.get();

                if(!result.success())
                {
                    errno = std::stoi(result.error().code);
                    return std::vector<list_containers_item>();
                }
                return result.response().containers;
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return std::vector<list_containers_item>();
            }
        }

        list_blobs_hierarchical_response blob_client_wrapper::list_blobs_hierarchical(const std::string &container, const std::string &delimiter, const std::string &continuation_token, const std::string &prefix)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return list_blobs_hierarchical_response();
            }
            if(container.length() == 0)
            {
                errno = invalid_parameters;
                return list_blobs_hierarchical_response();
            }

            try
            {
                auto task = m_blobClient->list_blobs_hierarchical(container, delimiter, continuation_token, prefix);
                task.wait();
                auto result = task.get();

                if(!result.success())
                {
                    errno = std::stoi(result.error().code);
                    //std::cout<< "error: " << result.error().code <<std::endl;
                    //std::cout<< "error: " << result.error().message <<std::endl;
                    return list_blobs_hierarchical_response();
                }
                else
                {
                    errno = 0;
                    return result.response();
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return list_blobs_hierarchical_response();
            }
        }

        void blob_client_wrapper::put_blob(const std::string &sourcePath, const std::string &container, const std::string blob, const std::vector<std::pair<std::string, std::string>> &metadata)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return;
            }
            if(sourcePath.length() == 0 || container.length() == 0 || blob.length() == 0)
            {
                errno = invalid_parameters;
                return;
            }

            std::ifstream ifs;
            try
            {
                ifs.open(sourcePath, std::ifstream::in);
            }
            catch(std::exception ex)
            {
                // TODO open failed
                errno = unknown_error;
                return;
            }

            try
            {
                auto task = m_blobClient->upload_block_blob_from_stream(container, blob, ifs, metadata);
                task.wait();
                auto result = task.get();
                if(!result.success())
                {
                    errno = std::stoi(result.error().code);
                }
                else
                {
                    errno = 0;
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
            }

            try
            {
                ifs.close();
            }
            catch(std::exception ex)
            {
                // TODO close failed
                errno = unknown_error;
            }
        }

        void blob_client_wrapper::upload_block_blob_from_stream(const std::string &container, const std::string blob, std::istream &is, const std::vector<std::pair<std::string, std::string>> &metadata)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return;
            }
            if(container.length() == 0 || blob.length() == 0)
            {
                errno = invalid_parameters;
                return;
            }

            try
            {
                auto task = m_blobClient->upload_block_blob_from_stream(container, blob, is, metadata);
                task.wait();
                auto result = task.get();
                if(!result.success())
                {
                    errno = std::stoi(result.error().code);
                }
                else
                {
                    errno = 0;
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
            }
        }

        void blob_client_wrapper::upload_file_to_blob(const std::string &sourcePath, const std::string &container, const std::string blob, const std::vector<std::pair<std::string, std::string>> &metadata, size_t parallel)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return;
            }
            if(sourcePath.length() == 0 || container.length() == 0 || blob.length() == 0)
            {
                errno = invalid_parameters;
                return;
            }

            off_t fileSize = get_file_size(sourcePath.c_str());
            if(fileSize < 0)
            {
                /*errno already set by stat.*/
                return;
            }

            if(fileSize <= 64*1024*1024)
            {
                put_blob(sourcePath, container, blob, metadata);
            }
            else
            {
//                std::cout << "fileSize: " << fileSize << std::endl;
                const int MaxBlockCount = 50000;
                long long MaxBlobSize = 4;
                MaxBlobSize *= MaxBlockCount;
                MaxBlobSize *= 1024 * 1024;
//                std::cout << "MazBlockSize: " << MaxBlobSize << std::endl;
                int block_size = 4*1024*1024;
                if(MaxBlobSize < fileSize)
                {
                    block_size = fileSize;
                }

                std::ifstream ifs;
                try
                {
                    ifs.open(sourcePath, std::ifstream::in);
                }
                catch(std::exception ex)
                {
                    // TODO open failed
                    errno = unknown_error;
                    return;
                }

                std::vector<put_block_list_request_base::block_item> block_list;
                std::vector<std::future<void>> task_list;
                std::mutex mutex;
                std::condition_variable cv;
                std::mutex cv_mutex;
                //const size_t mParallel = parallel;

                for(long long offset = 0, idx = 0; offset < fileSize; offset += block_size, ++idx)
                {
                    int length = block_size;
                    if(offset + length > fileSize)
                    {
                        length = fileSize - offset;
                    }

                    char* buffer = new char[block_size];
                    ifs.read(buffer, length);

                    std::string block_id = std::to_string(idx);
                    if(block_id.length() < 44)
                    {
                        block_id = (std::string(44 - block_id.length(), 'a')).append(block_id);
                    }
                    block_id = to_base64(block_id.c_str(), block_id.length());
                    put_block_list_request_base::block_item block;
                    block.id = block_id;
                    block.type = put_block_list_request_base::block_type::uncommitted;
                    block_list.push_back(block);

                    {
                        while(task_list.size() > m_concurrency)
                        {
                            for(auto iter = task_list.begin(); iter != task_list.end() && task_list.size() > m_concurrency; )
                            {
                                iter->wait();
                                iter = task_list.erase(iter);
                            }
                        }
                    }

                    if(errno != 0)
                    {
                        //std::cout << "errno: "<< errno<<std::endl;
                        break;
                    }
                    auto single_put = std::async(std::launch::async, [block_id, block_size, idx, this, buffer, offset, length, &container, &blob, &parallel, &mutex, &cv_mutex, &cv](){
                            {
                                std::unique_lock<std::mutex> lk(cv_mutex);
                                cv.wait(lk, [&parallel, &mutex]() {
                                        std::lock_guard<std::mutex> lock(mutex);
                                        if(parallel > 0)
                                        {
                                            --parallel;
                                            return true;
                                        }
                                        return false;
                                    });
                                //std::cout << "idx: " << idx << std::endl;
                                //std::cout << "parallel: " << parallel << std::endl;
                            }

                            std::istringstream in;
                            in.rdbuf()->pubsetbuf(buffer, length);
                            auto blockResult = m_blobClient->upload_block_from_stream(container, blob, block_id, in).get();
                            delete[] buffer;
                            if(!blockResult.success())
                            {
                                errno = std::stoi(blockResult.error().code);
                            }

                            {
                                std::lock_guard<std::mutex> lock(mutex);
                                ++parallel;
                                //std::cout << "idx done: " << idx << std::endl;
                                //std::cout << "parallel done: " << parallel << std::endl;
                                cv.notify_one();
                            }
                        });
                    //std::cout << "End: " << idx << std::endl;
                    task_list.push_back(std::move(single_put));
                }

                for(size_t i = 0; i < task_list.size(); ++i)
                    //while(!task_list.empty())
                {
                    //task_list.front().wait();
                    //task_list.pop();
                    task_list[i].wait();
                    if(errno != 0)
                    {
                        break;
                    }
                }

                //{
                //    std::unique_lock<std::mutex> lk(mutex);
                //    cv.wait(lk, [&parallel]() { return parallel == 8; });
                //}
                if(errno == 0)
                {
                    auto result = m_blobClient->put_block_list(container, blob, block_list, metadata).get();
                    if(!result.success())
                    {
                        // TODO upload failed
                        errno = std::stoi(result.error().code);
                    }
                }

                try
                {
                    ifs.close();
                }
                catch(std::exception ex)
                {
                    // TODO close failed
                    errno = unknown_error;
                }
            }
        }

        off_t get_file_size(const char* path)
        {
            struct stat st;
            if(stat(path, &st) == 0)
            {
                return st.st_size;
            }
            return -1;
        }

        void blob_client_wrapper::download_blob_to_stream(const std::string &container, const std::string &blob, unsigned long long offset, unsigned long long size, std::ostream &os)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return;
            }

            try
            {
                auto task = m_blobClient->download_blob_to_stream(container, blob, offset, size, os);
                task.wait();
                auto result = task.get();

                if(!result.success())
                {
                    errno = std::stoi(result.error().code);
                }
                else
                {
                    errno = 0;
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return;
            }
        }

        void blob_client_wrapper::download_blob_to_file(const std::string &container, const std::string &blob, const std::string &destPath, size_t parallel)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return;
            }

            const size_t downloaders = std::min(parallel, static_cast<size_t>(m_concurrency));
            try
            {
                // Download the first chunk of the blob. The response will contain required blob metadata as well.
                int errcode = 0;
                std::ofstream os(destPath.c_str(), std::ofstream::binary | std::ofstream::out);
                auto firstChunk = m_blobClient->get_chunk_to_stream_sync(container, blob, 0, DOWNLOAD_CHUNK_SIZE, os);
                os.close();
                if (!os) {
                    errno = unknown_error;
                    return;
                }
                if (!firstChunk.success())
                {
                    if (constants::code_request_range_not_satisfiable != firstChunk.error().code) {
                        errno = std::stoi(firstChunk.error().code);
                        return;
                    }
                    // The only reason for constants::code_request_range_not_satisfiable on the first chunk is zero
                    // blob size, so proceed as there is no error.
                }
                // Smoke check if the total size is known, otherwise - fail.
                if (firstChunk.response().totalSize < 0) {
                    errno = blob_no_content_range;
                    return;
                }

                // Get required metadata - etag to verify all future chunks and the total blob size.
                const auto originalEtag = firstChunk.response().etag;
                const auto length = static_cast<unsigned long long>(firstChunk.response().totalSize);

                // Resize the target file.
                auto fd = open(destPath.c_str(), O_WRONLY, 0770);
                if (-1 == fd) {
                    return;
                }
                if (-1 == ftruncate(fd, length)) {
                    close(fd);
                    return;
                }
                close(fd);

                // Download the rest.
                const auto left = length - firstChunk.response().size;
                const auto chunk_size = std::max(DOWNLOAD_CHUNK_SIZE, (left + downloaders - 1)/ downloaders);
                std::vector<std::future<int>> task_list;
                for(unsigned long long offset = firstChunk.response().size; offset < length; offset += chunk_size)
                {
                    const auto range = std::min(chunk_size, length - offset);
                    auto single_download = std::async(std::launch::async, [originalEtag, offset, range, this, &destPath, &container, &blob](){
                            // Note, keep std::ios_base::in to prevent truncating of the file.
                            std::ofstream output(destPath.c_str(), std::ios_base::out |  std::ios_base::in);
                            output.seekp(offset);
                            auto chunk = m_blobClient->get_chunk_to_stream_sync(container, blob, offset, range, output);
                            output.close();
                            if(!chunk.success())
                            {
                                // Looks like the blob has been replaced by smaller one - ask user to retry.
                                if (constants::code_request_range_not_satisfiable == chunk.error().code) {
                                    return EAGAIN;
                                }
                                return std::stoi(chunk.error().code);
                            }
                            // The etag has been changed - ask user to retry.
                            if (originalEtag != chunk.response().etag) {
                                return EAGAIN;
                            }
                            // Check for any writing errors.
                            if (!output) {
                                return unknown_error;
                            }
                            return 0;
                        });
                    task_list.push_back(std::move(single_download));
                }

                // Wait for workers to complete downloading.
                for(size_t i = 0; i < task_list.size(); ++i)
                {
                    task_list[i].wait();
                    auto result = task_list[i].get();
                    // let's report the first encountered error for consistency.
                    if (0 != result && errcode == 0) {
                        errcode = result;
                    }
                }
                errno = errcode;
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return;
            }
        }

        blob_property blob_client_wrapper::get_blob_property(const std::string &container, const std::string &blob)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return blob_property(false);
            }

            try
            {
                auto result = m_blobClient->get_blob_property(container, blob);
                if(!result.success())
                {
                    errno = std::stoi(result.error().code);
                    return blob_property(false);
                }
                else
                {
                    errno = 0;
                    return result.response();
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return blob_property(false);
            }
        }

        bool blob_client_wrapper::blob_exists(const std::string &container, const std::string &blob)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return false;
            }

            try
            {
                auto blobProperty = get_blob_property(container, blob);
                if(blobProperty.valid())
                {
                    errno = 0;
                    return true;
                }
                return false;
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return false;
            }
        }

        void blob_client_wrapper::delete_blob(const std::string &container, const std::string &blob)
        {
            if(!is_valid())
            {
                errno = client_not_init;
                return;
            }
            if(container.length() == 0 || blob.length() == 0)
            {
                errno = invalid_parameters;
                return;
            }

            try
            {
                auto task = m_blobClient->delete_blob(container, blob);
                task.wait();
                auto result = task.get();

                if(!result.success())
                {
                    errno = std::stoi(result.error().code);
                }
                else
                {
                    errno = 0;
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return;
            }
        }

        void blob_client_wrapper::start_copy(const std::string &sourceContainer, const std::string &sourceBlob, const std::string &destContainer, const std::string &destBlob)
        {

            if(!is_valid())
            {
                errno = client_not_init;
                return;
            }
            if(sourceContainer.length() == 0 || sourceBlob.length() == 0 ||
               destContainer.length() == 0 || destBlob.length() == 0)
            {
                errno = invalid_parameters;
                return;
            }

            try
            {
                auto task = m_blobClient->start_copy(sourceContainer, sourceBlob, destContainer, destBlob);
                task.wait();
                auto result = task.get();

                if(!result.success())
                {
                    errno = std::stoi(result.error().code);
                }
                else
                {
                    errno = 0;
                }
            }
            catch(std::exception ex)
            {
                errno = unknown_error;
                return;
            }
        }

    }
} // microsoft_azure::storage
