/* Example
#include <spdlog_mpi_logger.h>

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    auto logger = std::make_shared<spdlog::mpi_logger>("my_mpi_logger");
    logger->info("ceci est un test");
    MPI_Finalize();
    return 0;
}
*/

#pragma once

#include <iostream>
#include <spdlog/logger.h>
#include <mpi.h>

namespace spdlog {

class mpi_logger: public logger
{
public:
    // Empty logger
    explicit mpi_logger(std::string logger_name, int tag_ = 4444, int server_rank_ = 0, MPI_Comm comm_ = MPI_COMM_WORLD)
        : logger(std::move(logger_name))
        , tag(tag_), server_rank(server_rank_), comm(comm_)
    {
        MPI_Comm_rank(comm, &mpi_rank);
        if (mpi_rank == server_rank) {
            server_thr = std::thread([&](){
                int i = 4;
                while(i--) {
                    MPI_Status status;
                    MPI_Probe(MPI_ANY_SOURCE, tag, comm, &status);
                    int msg_size = 0;
                    MPI_Get_count(&status, MPI_BYTE, &msg_size);
                    auto buf = std::make_unique<char[]>(msg_size);
                    MPI_Recv(buf.get(), msg_size, MPI_PACKED, MPI_ANY_SOURCE, tag, comm, MPI_STATUS_IGNORE);

                    int position = 0;
                    int level = 0;
                    MPI_Unpack(buf.get(), msg_size, &position, &level, 1, MPI_INT, comm);
                    unsigned long payload_size = 0;
                    MPI_Unpack(buf.get(), msg_size, &position, &payload_size, 1, MPI_UNSIGNED_LONG, comm);
                    std::string payload(payload_size, ' ');
                    MPI_Unpack(buf.get(), msg_size, &position, &payload[0], payload_size, MPI_BYTE, comm);
                    std::cout << "[" << level << "] " << payload << std::endl;
                }
            });
        }
    }

    ~mpi_logger() override {
        if (mpi_rank == server_rank)
        server_thr.join();
    }

protected:
    void sink_it_(const details::log_msg &msg) override {
        int msg_size = 0;
        int elem_size = 0;
        MPI_Pack_size(1, MPI_INT, comm, &elem_size); // level
        msg_size += elem_size;
        MPI_Pack_size(1, MPI_UNSIGNED_LONG, comm, &elem_size); // size of payload
        msg_size += elem_size;
        MPI_Pack_size(msg.payload.size(), MPI_BYTE, comm, &elem_size); // payload data
        msg_size += elem_size;

        auto buf = std::make_unique<char[]>(msg_size);
        int position = 0;
        MPI_Pack(&(msg.level), 1, MPI_INT, buf.get(), msg_size, &position, comm);
        auto payload_size = msg.payload.size();
        MPI_Pack(&payload_size, 1, MPI_UNSIGNED_LONG, buf.get(), msg_size, &position, comm);
        const char* payload = msg.payload.data();
        MPI_Pack(&payload, payload_size, MPI_BYTE, buf.get(), msg_size, &position, comm);
        assert(position == msg_size);
        MPI_Send(buf.get(), position, MPI_PACKED, server_rank, tag, comm);
    }
    void flush_() override {}

private:
    int tag;
    int server_rank;
    MPI_Comm comm;

    int mpi_rank;

    std::thread server_thr;
};

} // namespace spdlog
