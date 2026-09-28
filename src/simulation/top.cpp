#define SC_INCLUDE_DYNAMIC_PROCESSES

#include <systemc.h>
#include <tlm.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "../traffic/cpu_traffic_generator.h"
#include "../traffic/dma_model.h"
#include "../traffic/dnn_model.h"
#include "../traffic/memory_scheduler_stress.h"
#include "../traffic/memory_bandwidth_sweep.h"

#include "../axi/axi_interconnect.h"
#include "../axi/axi_extension.h"

#include "../arbitration/qos_priority.h"
#include "../memory/memory_controller.h"


/*
 * ------------------------------------------------------------
 * Percentile helper
 * ------------------------------------------------------------
 *
 * Input:
 *     latency samples in nanoseconds
 *
 * Output:
 *     percentile using linear interpolation
 */
double percentile(
    std::vector<double> values,
    double percentile_value)
{
    if (values.empty()) {
        return 0.0;
    }

    std::sort(
        values.begin(),
        values.end()
    );

    if (values.size() == 1) {
        return values.front();
    }

    double position =
        (percentile_value / 100.0) *
        static_cast<double>(
            values.size() - 1
        );

    std::size_t lower =
        static_cast<std::size_t>(
            position
        );

    std::size_t upper =
        lower + 1;

    if (upper >= values.size()) {
        return values.back();
    }

    double fraction =
        position -
        static_cast<double>(lower);

    return
        values[lower] +
        fraction *
        (
            values[upper] -
            values[lower]
        );
}


double average(
    const std::vector<double>& values)
{
    if (values.empty()) {
        return 0.0;
    }

    double sum = 0.0;

    for (double value : values) {
        sum += value;
    }

    return
        sum /
        static_cast<double>(
            values.size()
        );
}


/*
 * ------------------------------------------------------------
 * Simulation configuration
 * ------------------------------------------------------------
 *
 * Phase 17:
 *     Uses the default configuration.
 *
 * Phase 18:
 *     Parameters can be supplied through:
 *
 *     ./soc_sim --dse
 *         CPU_QOS
 *         DMA_QOS
 *         DNN_QOS
 *         CPU_OUTSTANDING
 *         DMA_OUTSTANDING
 *         DNN_OUTSTANDING
 *         AXI_QUEUE_DEPTH
 *
 * Example:
 *
 *     ./soc_sim --dse 15 8 4 8 16 32 16
 */
struct SimulationConfig
{
    unsigned int cpu_qos;
    unsigned int dma_qos;
    unsigned int dnn_qos;

    unsigned int cpu_outstanding;
    unsigned int dma_outstanding;
    unsigned int dnn_outstanding;

    unsigned int axi_queue_depth;
};


/*
 * ------------------------------------------------------------
 * Print usage
 * ------------------------------------------------------------
 */
void print_usage()
{
    std::cout
        << "\nUsage:\n"
        << "  ./soc_sim\n"
        << "      Run normal Phase 17 workload using baseline configuration.\n\n"

        << "  ./soc_sim --stress\n"
        << "      Run Phase 14 memory scheduler stress test.\n\n"

        << "  ./soc_sim --bandwidth\n"
        << "      Run Phase 15/16 memory bandwidth and statistics sweep.\n\n"

        << "  ./soc_sim --dse "
        << "CPU_QOS DMA_QOS DNN_QOS "
        << "CPU_OUTSTANDING DMA_OUTSTANDING "
        << "DNN_OUTSTANDING AXI_QUEUE_DEPTH\n"
        << "      Run one configurable Phase 18 DSE point.\n\n"

        << "Example:\n"
        << "  ./soc_sim --dse 15 8 4 8 16 32 16\n\n";
}


/*
 * ------------------------------------------------------------
 * Main
 * ------------------------------------------------------------
 */
int sc_main(
    int argc,
    char* argv[])
{
    /*
     * --------------------------------------------------------
     * Default configuration
     * --------------------------------------------------------
     */
    SimulationConfig config;

    config.cpu_qos = 15;
    config.dma_qos = 8;
    config.dnn_qos = 4;

    config.cpu_outstanding = 8;
    config.dma_outstanding = 16;
    config.dnn_outstanding = 32;

    config.axi_queue_depth = 16;


    /*
     * --------------------------------------------------------
     * Command-line modes
     * --------------------------------------------------------
     */
    bool stress_mode = false;
    bool bandwidth_mode = false;
    bool dse_mode = false;


    if (argc > 1) {

        std::string mode =
            argv[1];


        /*
         * ----------------------------------------------------
         * Phase 14 stress mode
         * ----------------------------------------------------
         */
        if (mode == "--stress") {

            stress_mode = true;
        }


        /*
         * ----------------------------------------------------
         * Phase 15/16 bandwidth mode
         * ----------------------------------------------------
         */
        else if (mode == "--bandwidth") {

            bandwidth_mode = true;
        }


        /*
         * ----------------------------------------------------
         * Phase 18 DSE mode
         *
         * Expected arguments:
         *
         * argv[1] = --dse
         * argv[2] = CPU QoS
         * argv[3] = DMA QoS
         * argv[4] = DNN QoS
         * argv[5] = CPU outstanding
         * argv[6] = DMA outstanding
         * argv[7] = DNN outstanding
         * argv[8] = AXI queue depth
         * ----------------------------------------------------
         */
        else if (mode == "--dse") {

            dse_mode = true;

            if (argc != 9) {

                std::cerr
                    << "\nERROR: --dse requires exactly "
                    << "7 configuration values.\n";

                print_usage();

                return 1;
            }

            try {

                config.cpu_qos =
                    static_cast<unsigned int>(
                        std::stoul(argv[2])
                    );

                config.dma_qos =
                    static_cast<unsigned int>(
                        std::stoul(argv[3])
                    );

                config.dnn_qos =
                    static_cast<unsigned int>(
                        std::stoul(argv[4])
                    );

                config.cpu_outstanding =
                    static_cast<unsigned int>(
                        std::stoul(argv[5])
                    );

                config.dma_outstanding =
                    static_cast<unsigned int>(
                        std::stoul(argv[6])
                    );

                config.dnn_outstanding =
                    static_cast<unsigned int>(
                        std::stoul(argv[7])
                    );

                config.axi_queue_depth =
                    static_cast<unsigned int>(
                        std::stoul(argv[8])
                    );
            }

            catch (const std::exception&) {

                std::cerr
                    << "\nERROR: Invalid numeric value "
                    << "provided to --dse.\n";

                print_usage();

                return 1;
            }


            /*
             * Basic parameter validation.
             */
            if (
                config.cpu_qos == 0 ||
                config.dma_qos == 0 ||
                config.dnn_qos == 0 ||
                config.cpu_outstanding == 0 ||
                config.dma_outstanding == 0 ||
                config.dnn_outstanding == 0 ||
                config.axi_queue_depth == 0
            ) {

                std::cerr
                    << "\nERROR: All DSE parameters "
                    << "must be greater than zero.\n";

                return 1;
            }
        }


        /*
         * ----------------------------------------------------
         * Unknown mode
         * ----------------------------------------------------
         */
        else {

            std::cerr
                << "\nERROR: Unknown command-line option: "
                << mode
                << "\n";

            print_usage();

            return 1;
        }
    }


    /*
     * ------------------------------------------------------------
     * Common memory configuration
     * ------------------------------------------------------------
     */
    const unsigned int COLUMN_BITS = 8;
    const unsigned int BANK_BITS = 3;
    const unsigned int CHANNEL_BITS = 0;

    const unsigned int MEMORY_SCHED_QUEUE_DEPTH = 16;


    /*
     * ------------------------------------------------------------
     * Phase 14 stress mode
     * ------------------------------------------------------------
     */
    if (stress_mode) {

        std::cout
            << "--- Phase 14: Memory Scheduler Stress Test ---"
            << std::endl;


        MemoryController memory(
            "MEMORY",
            COLUMN_BITS,
            BANK_BITS,
            CHANNEL_BITS,
            sc_time(50, SC_NS),
            sc_time(120, SC_NS),
            MEMORY_SCHED_QUEUE_DEPTH
        );


        MemorySchedulerStress stress(
            "MEMORY_SCHED_STRESS"
        );


        stress.socket.bind(
            memory.socket
        );


        sc_start();

        return 0;
    }


    /*
     * ------------------------------------------------------------
     * Phase 15/16 bandwidth mode
     * ------------------------------------------------------------
     */
    if (bandwidth_mode) {

        std::cout
            << "--- Phase 15/16: Memory Bandwidth + Statistics ---"
            << std::endl;


        MemoryController memory(
            "MEMORY",
            COLUMN_BITS,
            BANK_BITS,
            CHANNEL_BITS,
            sc_time(50, SC_NS),
            sc_time(120, SC_NS),
            MEMORY_SCHED_QUEUE_DEPTH
        );


        MemoryBandwidthSweep bandwidth(
            "MEMORY_BANDWIDTH_SWEEP"
        );


        /*
         * Phase 15 intentionally bypasses the AXI
         * interconnect and directly exercises the memory
         * subsystem.
         */
        bandwidth.socket.bind(
            memory.socket
        );


        bandwidth.set_memory_controller(
            memory
        );


        sc_start();

        return 0;
    }


    /*
     * ------------------------------------------------------------
     * Phase 17 / Phase 18
     * Normal SoC workload
     * ------------------------------------------------------------
     */

    std::cout
        << "\n============================================\n";


    if (dse_mode) {

        std::cout
            << " PHASE 18: DSE CONFIGURATION RUN\n";
    }

    else {

        std::cout
            << " PHASE 17: END-TO-END PERFORMANCE METRICS\n";
    }


    std::cout
        << "============================================\n";


    std::cout
        << "CPU QoS               : "
        << config.cpu_qos
        << "\n";


    std::cout
        << "DMA QoS               : "
        << config.dma_qos
        << "\n";


    std::cout
        << "DNN QoS               : "
        << config.dnn_qos
        << "\n";


    std::cout
        << "CPU outstanding       : "
        << config.cpu_outstanding
        << "\n";


    std::cout
        << "DMA outstanding       : "
        << config.dma_outstanding
        << "\n";


    std::cout
        << "DNN outstanding       : "
        << config.dnn_outstanding
        << "\n";


    std::cout
        << "AXI queue depth       : "
        << config.axi_queue_depth
        << "\n";


    std::cout
        << "============================================\n\n";


    /*
     * ------------------------------------------------------------
     * Instantiate traffic generators
     * ------------------------------------------------------------
     */
    CpuTrafficGenerator cpu(
        "CPU",
        config.cpu_qos,
        config.cpu_outstanding
    );


    DmaModel dma(
        "DMA",
        config.dma_qos,
        config.dma_outstanding
    );


    DnnModel dnn(
        "DNN",
        config.dnn_qos,
        config.dnn_outstanding
    );


    /*
     * ------------------------------------------------------------
     * AXI interconnect
     * ------------------------------------------------------------
     */
    AxiInterconnect interconnect(
        "INTERCONNECT",
        config.axi_queue_depth
    );


    /*
     * Existing QoS arbitration.
     */
    interconnect.set_policy(
        new QosPriorityPolicy()
    );


    /*
     * ------------------------------------------------------------
     * Memory controller
     * ------------------------------------------------------------
     */
    MemoryController memory(
        "MEMORY",
        COLUMN_BITS,
        BANK_BITS,
        CHANNEL_BITS,
        sc_time(50, SC_NS),
        sc_time(120, SC_NS),
        MEMORY_SCHED_QUEUE_DEPTH
    );


    /*
     * ------------------------------------------------------------
     * Socket connections
     * ------------------------------------------------------------
     */
    cpu.socket.bind(
        interconnect.cpu_socket
    );


    dma.socket.bind(
        interconnect.dma_socket
    );


    dnn.socket.bind(
        interconnect.dnn_socket
    );


    interconnect.mem_socket.bind(
        memory.socket
    );


    /*
     * ------------------------------------------------------------
     * Run complete workload
     * ------------------------------------------------------------
     */
    sc_start();


    /*
     * ------------------------------------------------------------
     * Phase 17 / Phase 18:
     * Collect CPU latency statistics
     * ------------------------------------------------------------
     */
    const std::vector<double>&
        cpu_latencies =
            cpu.get_latency_samples();


    double cpu_avg_ns =
        average(cpu_latencies);


    double cpu_p50_ns =
        percentile(
            cpu_latencies,
            50.0
        );


    double cpu_p95_ns =
        percentile(
            cpu_latencies,
            95.0
        );


    double cpu_p99_ns =
        percentile(
            cpu_latencies,
            99.0
        );


    double cpu_max_ns = 0.0;


    if (!cpu_latencies.empty()) {

        cpu_max_ns =
            *std::max_element(
                cpu_latencies.begin(),
                cpu_latencies.end()
            );
    }


    /*
     * ------------------------------------------------------------
     * Determine overall workload interval.
     *
     * All three generators currently begin at simulation
     * time zero, but calculate the interval from their
     * recorded timestamps.
     * ------------------------------------------------------------
     */
    sc_time first_issue =
        SC_ZERO_TIME;


    sc_time last_completion =
        SC_ZERO_TIME;


    bool have_start = false;
    bool have_end = false;


    /*
     * CPU activity
     */
    if (cpu.has_activity()) {

        first_issue =
            cpu.get_first_issue_time();


        last_completion =
            cpu.get_last_completion_time();


        have_start = true;
        have_end = true;
    }


    /*
     * DMA activity
     */
    if (dma.has_activity()) {

        sc_time dma_start =
            dma.get_first_issue_time();


        sc_time dma_end =
            dma.get_last_completion_time();


        if (
            !have_start ||
            dma_start < first_issue
        ) {

            first_issue =
                dma_start;
        }


        if (
            !have_end ||
            dma_end > last_completion
        ) {

            last_completion =
                dma_end;
        }


        have_start = true;
        have_end = true;
    }


    /*
     * DNN activity
     */
    if (dnn.has_activity()) {

        sc_time dnn_start =
            dnn.get_first_issue_time();


        sc_time dnn_end =
            dnn.get_last_completion_time();


        if (
            !have_start ||
            dnn_start < first_issue
        ) {

            first_issue =
                dnn_start;
        }


        if (
            !have_end ||
            dnn_end > last_completion
        ) {

            last_completion =
                dnn_end;
        }


        have_start = true;
        have_end = true;
    }


    double workload_time_ns = 0.0;


    if (have_start && have_end) {

        workload_time_ns =
            (
                last_completion -
                first_issue
            ).to_seconds() * 1.0e9;
    }


    /*
     * ------------------------------------------------------------
     * Accelerator throughput
     *
     * Aggregate DMA + DNN successfully completed bytes.
     * ------------------------------------------------------------
     */
    unsigned long long dma_bytes =
        dma.get_completed_bytes();


    unsigned long long dnn_bytes =
        dnn.get_completed_bytes();


    unsigned long long accelerator_bytes =
        dma_bytes +
        dnn_bytes;


    double accelerator_throughput_gbps =
        0.0;


    if (workload_time_ns > 0.0) {

        /*
         * bytes/ns numerically equals decimal GB/s:
         *
         * 1 GB/s = 1 byte/ns
         */
        accelerator_throughput_gbps =
            static_cast<double>(
                accelerator_bytes
            ) /
            workload_time_ns;
    }


    /*
     * ------------------------------------------------------------
     * Print results
     * ------------------------------------------------------------
     */
    std::cout
        << "\n============================================\n";


    if (dse_mode) {

        std::cout
            << " PHASE 18 DSE RESULTS\n";
    }

    else {

        std::cout
            << " PHASE 17 RESULTS\n";
    }


    std::cout
        << "============================================\n";


    std::cout
        << std::fixed
        << std::setprecision(2);


    std::cout
        << "CPU completed requests : "
        << cpu.get_completed_requests()
        << "\n";


    std::cout
        << "CPU average latency   : "
        << cpu_avg_ns
        << " ns\n";


    std::cout
        << "CPU P50 latency       : "
        << cpu_p50_ns
        << " ns\n";


    std::cout
        << "CPU P95 latency       : "
        << cpu_p95_ns
        << " ns\n";


    std::cout
        << "CPU P99 latency       : "
        << cpu_p99_ns
        << " ns\n";


    std::cout
        << "CPU max latency       : "
        << cpu_max_ns
        << " ns\n";


    std::cout
        << "DMA completed bytes   : "
        << dma_bytes
        << "\n";


    std::cout
        << "DNN completed bytes   : "
        << dnn_bytes
        << "\n";


    std::cout
        << "Accelerator bytes     : "
        << accelerator_bytes
        << "\n";


    std::cout
        << "Workload duration     : "
        << workload_time_ns
        << " ns\n";


    std::cout
        << "Accelerator throughput: "
        << accelerator_throughput_gbps
        << " GB/s\n";


    std::cout
        << "============================================\n";


    /*
     * ------------------------------------------------------------
     * CSV output
     *
     * One row per simulation.
     *
     * Phase 18 Python DSE can execute the simulator many
     * times and collect these rows into a larger dataset.
     * ------------------------------------------------------------
     */
    std::ofstream csv(
        "phase17_end_to_end.csv"
    );


    csv
        << "policy,"
        << "cpu_qos,"
        << "dma_qos,"
        << "dnn_qos,"
        << "cpu_outstanding,"
        << "dma_outstanding,"
        << "dnn_outstanding,"
        << "axi_queue_depth,"
        << "cpu_completed_requests,"
        << "cpu_average_latency_ns,"
        << "cpu_p50_latency_ns,"
        << "cpu_p95_latency_ns,"
        << "cpu_p99_latency_ns,"
        << "cpu_max_latency_ns,"
        << "dma_completed_bytes,"
        << "dnn_completed_bytes,"
        << "accelerator_bytes,"
        << "workload_duration_ns,"
        << "accelerator_throughput_gbps\n";


    csv
        << "qos,"
        << config.cpu_qos
        << ","
        << config.dma_qos
        << ","
        << config.dnn_qos
        << ","
        << config.cpu_outstanding
        << ","
        << config.dma_outstanding
        << ","
        << config.dnn_outstanding
        << ","
        << config.axi_queue_depth
        << ","
        << cpu.get_completed_requests()
        << ","
        << cpu_avg_ns
        << ","
        << cpu_p50_ns
        << ","
        << cpu_p95_ns
        << ","
        << cpu_p99_ns
        << ","
        << cpu_max_ns
        << ","
        << dma_bytes
        << ","
        << dnn_bytes
        << ","
        << accelerator_bytes
        << ","
        << workload_time_ns
        << ","
        << accelerator_throughput_gbps
        << "\n";


    csv.close();


    /*
     * ------------------------------------------------------------
     * Machine-readable result line
     *
     * Python DSE can parse this without depending on
     * human-readable debug output.
     * ------------------------------------------------------------
     */
    std::cout
        << "PHASE18_RESULT"
        << " policy=qos"
        << " cpu_qos=" << config.cpu_qos
        << " dma_qos=" << config.dma_qos
        << " dnn_qos=" << config.dnn_qos
        << " cpu_outstanding=" << config.cpu_outstanding
        << " dma_outstanding=" << config.dma_outstanding
        << " dnn_outstanding=" << config.dnn_outstanding
        << " axi_queue_depth=" << config.axi_queue_depth
        << " cpu_avg_ns=" << cpu_avg_ns
        << " cpu_p50_ns=" << cpu_p50_ns
        << " cpu_p95_ns=" << cpu_p95_ns
        << " cpu_p99_ns=" << cpu_p99_ns
        << " cpu_max_ns=" << cpu_max_ns
        << " accelerator_throughput_gbps="
        << accelerator_throughput_gbps
        << std::endl;


    std::cout
        << "\n[PHASE18] Results written to "
        << "phase17_end_to_end.csv"
        << std::endl;


    return 0;
}
