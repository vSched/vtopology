


/*
 * Copyright (C) 2018-2019 VMware, Inc.
 * SPDX-License-Identifier: GPL-2.0
 */
#include <set>
#include <inttypes.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <limits.h>
#include <assert.h>
#include <iostream>
#include <string.h>
#include <random>
#include <vector>
#include <fstream>
#include <dirent.h>
#include <signal.h>
#include <sstream>
#include <sys/syscall.h>
#include <unordered_map>
#include <bitset>
#include <filesystem>

#define PROBE_MODE	(0)
#define DIRECT_MODE	(1)

#define MAX_CPUS	(192)
#define GROUP_LOCAL	(0)
#define GROUP_NONLOCAL	(1)
#define GROUP_GLOBAL	(2)

#define NUMA_GROUP	(0)
#define PAIR_GROUP	(1)
#define THREAD_GROUP	(2)


#define min(a,b)	(a < b ? a : b)
#define LAST_CPU_ID	(min(nr_cpus, MAX_CPUS))


typedef unsigned atomic_t;



int nr_cpus;
//parameters
int verbose = 0;
int NR_SAMPLES = 2;
int SAMPLE_US = 200000;
int act_sample = 10;

int all_samples_found = 0;

int sleep_time = 4;
bool first_measurement = false;
static size_t nr_relax = 1;
int nr_numa_groups = 0;
int nr_pair_groups = 0;
int nr_tt_groups = 0;
int minimum_latency_4 = 0;
double threefour_latency_class = 8500;
int cpu_group_id[MAX_CPUS];
int cpu_pair_id[MAX_CPUS];
int cpu_tt_id[MAX_CPUS];


bool failed_test = false;
bool changed_allowance = false;
int latency_valid = -1;
int nr_param = 150;
namespace fs = std::filesystem;
std::vector<std::vector<int>> numa_to_pair_arr;
std::vector<std::vector<int>> pair_to_thread_arr;
std::vector<std::vector<int>> thread_to_cpu_arr;
std::vector<int> vcap_banned;
std::filesystem::path banlistPath = "../banlist";
std::vector<int> numas_to_cpu;
std::vector<int> pairs_to_cpu;
std::vector<int> threads_to_cpu;
std::vector<std::vector<int>> top_stack;
pthread_t worker_tasks[MAX_CPUS];
pthread_mutex_t top_stack_mutex = PTHREAD_MUTEX_INITIALIZER;

std::vector<pid_t> stopped_processes;




void giveTopologyToKernel(){
        std::string output_str = "";
	std::vector<bool> output_bits;

	int bit_index = 0;
        for(int j = 3;j<6;j++){
                for (int i = 0; i < LAST_CPU_ID; i++) {
                        for(int p=0;p< LAST_CPU_ID;p++){
                                if(top_stack[i][p]<j){
					output_str+="1";
                                        output_bits.push_back(true);
                                }else{
                                        output_str+="0";
					output_bits.push_back(false);
                                }
                        }
                        output_str+=";";
                }
                output_str+=":";
        }

     std::ofstream procFile("/proc/vtopology_write", std::ios::out | std::ios::trunc);
     // Convert bit vector to byte array
    if (procFile.is_open()) {
	        std::vector<uint8_t> byteArray((output_bits.size() + 7) / 8, 0);  // Create a byte array to hold the bits
        
        for (size_t i = 0; i < output_bits.size(); ++i) {
            if (output_bits[i]) {
                byteArray[i / 8] |= (1 << (i % 8));  // Set the bit in the appropriate byte
            }
        }
        
        procFile.write(reinterpret_cast<const char*>(byteArray.data()), byteArray.size());


        procFile.close();
    } else {
        std::cerr << "Error: Unable to open /proc/edit_topology for writing." << std::endl;
    }
}



void updateVectorFromBanlist(std::string fileLocation) {
    std::ifstream file(fileLocation);

    if (!file) {
        std::cerr << "Failed to access file" << std::endl;
        return;
    }

    // Set all elements of vtop_banned to 0
    std::fill(vcap_banned.begin(), vcap_banned.end(), 0);
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string item;
        while (std::getline(iss, item, ',')) {
            item.erase(0, item.find_first_not_of(" \t"));
            item.erase(item.find_last_not_of(" \t") + 1);

            try {
                int index = std::stoi(item);
                if (index >= 0 && index < vcap_banned.size()) {
                        vcap_banned[index] = 1;
			std::cout<<"HERE:"<<index<<std::endl;
                }
            } catch (const std::invalid_argument& e) {
                continue;
            }
        }
    }
    file.close();
}

bool toggle_CPU_active(int cpuNumber, bool active) {

/**
    std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(cpuNumber) + "/online";
     std::string command = "echo " + std::string(active ? "1" : "0") + 
                          " | sudo tee /sys/devices/system/cpu/cpu" + std::to_string(cpuNumber) + "/online";

    // Execute the command
    int result = system(command.c_str());

    if (!file.is_open()) {
        std::cout << "Error: Unable to open " << path << ". Check if the CPU number is correct and you have the necessary permissions." << std::endl;
        return false;
    }


    if (!file.good()) {
        std::cout << "Error: Failed to write to " << path << std::endl;
        file.close();
        return false;
    }

    file.close();
**/
    return true;
}


void enableAllCpus(){
	//std::string command = "sudo cset set --cpu 0- " + std::to_string(LAST_CPU_ID)+" --set=benchmark_cpuset";
	//int result = system(command.c_str());
	std::ofstream banlistFile(banlistPath / "vtop.txt");
        if (banlistFile.is_open()) {
                banlistFile << "";
                banlistFile.close();
                std::cout << "Banlist written to /home/banlist.txt" << std::endl;
        } else {
                std::cout << "Unable to open file /home/banlist.txt" << std::endl;
        }

}


bool SetCpuAffinity(pid_t pid, const cpu_set_t &cpuSet) {
    if (sched_setaffinity(pid, sizeof(cpuSet), &cpuSet) == -1) {
        std::cerr << "Failed to set CPU affinity: " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

void disableStackingCpus(){
//	std::vector<std::vector<int>> cust_thread_to_cpu_arr = thread_to_cpu_arr;
	//return;
	std::vector<int> has_been_disqualified(LAST_CPU_ID);
	std::vector<int> thread_cpu_mask;
	bool not_first;
	bool previously =changed_allowance;
	int total=0;
	std::string banlist="";
	std::set<int> banset;
	for(int z=0;z<thread_to_cpu_arr.size();z++){
		thread_cpu_mask = thread_to_cpu_arr[z];
		total=0;
		for(int x=0;x<thread_cpu_mask.size();x++){
				if(total>0 && thread_cpu_mask[x]){
                                        //toggle_CPU_active(x,0);
					changed_allowance = true;
					banset.insert(x);
					banlist+=(std::to_string(x)+",");
				}
				if(thread_cpu_mask[x]){
					total+=1;
				}
		}
    	}
//	if (!banlist.empty()) {
//        	banlist.pop_back();
//    	}
	std::ofstream banlistFile(banlistPath / "vtop.txt");
	if (banlistFile.is_open()) {
    		banlistFile << banlist;
    		banlistFile.close();
	} else {
    		std::cout << "Unable to open file /home/banlist.txt" << std::endl;
	}
    	
}




void moveCurrentThread() {
    pid_t tid;
    tid = syscall(SYS_gettid);
    std::string path = "/sys/fs/cgroup/hi_prgroup/cgroup.procs";
    std::ofstream ofs(path, std::ios_base::app);
    if (!ofs) {
        std::cerr << "Could not open the file\n";
        return;
    }
    ofs << tid << "\n";
    ofs.close();
    //struct sched_param params;
    //params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    //sched_setscheduler(tid,SCHED_FIFO,&params);
}

std::string_view get_option(
    const std::vector<std::string_view>& args, 
    const std::string_view& option_name) {
    for (auto it = args.begin(), end = args.end(); it != end; ++it) {
        if (*it == option_name)
            if (it + 1 != end)
                return *(it + 1);
    }
    
    return "";
};


bool has_option(
    const std::vector<std::string_view>& args, 
    const std::string_view& option_name) {
    for (auto it = args.begin(), end = args.end(); it != end; ++it) {
        if (*it == option_name)
            return true;
    }
    
    return false;
};


void setArguments(const std::vector<std::string_view>& arguments) {
    verbose = has_option(arguments, "-v");
   // banlistPath = getBanlistPath();
    auto set_option_value = [&](const std::string_view& option, int& target) {
        if (auto value = get_option(arguments, option); !value.empty()) {
            try {
                target = std::stoi(std::string(value));
            } catch(const std::invalid_argument&) {
                throw std::invalid_argument(std::string("Invalid argument for option ") + std::string(option));
            } catch(const std::out_of_range&) {
                throw std::out_of_range(std::string("Out of range argument for option ") + std::string(option));
            }
        }
    };
    
    set_option_value("-s", NR_SAMPLES);
    set_option_value("-u", SAMPLE_US);
    set_option_value("-d",nr_param);
	set_option_value("-g",act_sample);
    set_option_value("-f",sleep_time);
}


typedef union {
	atomic_t x;
	char pad[1024];
} big_atomic_t __attribute__((aligned(1024)));
                                                                  
struct thread_args_t {
    cpu_set_t cpus;
    atomic_t me;
    atomic_t buddy;
    big_atomic_t* nr_pingpongs;
    atomic_t** pingpong_mutex;
    int* stoploops;
    std::vector<uint64_t> timestamps;
    pthread_mutex_t* mutex;
    pthread_cond_t* cond;
    int* flag;
	int* max_loops;
	bool* prepared;

    thread_args_t(int cpu_id, atomic_t me_value, atomic_t buddy_value,atomic_t** pp_mutex, big_atomic_t* nr_pp, int* stop_loops, pthread_mutex_t* mtx, pthread_cond_t* cond, int* flag,bool* prep, int* max_loops)
        : me(me_value), buddy(buddy_value), nr_pingpongs(nr_pp), pingpong_mutex(pp_mutex), stoploops(stop_loops), mutex(mtx), cond(cond), flag(flag), prepared(prep), max_loops(max_loops) {
        CPU_ZERO(&cpus);
        CPU_SET(cpu_id, &cpus);
    }
};



static inline uint64_t now_nsec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * ((uint64_t)1000*1000*1000) + ts.tv_nsec;
}

static void common_setup(thread_args_t *args)
{
	if (sched_setaffinity(0, sizeof(cpu_set_t), &args->cpus)) {
		perror("sched_setaffinity");
		exit(1);
	}

	if (args->me == 0) {
		*(args->pingpong_mutex) = (atomic_t*)mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
		if (*(args->pingpong_mutex) == MAP_FAILED) {
			perror("mmap");
			exit(1);
		}
		*(*(args->pingpong_mutex)) = args->me;
	}

	// ensure both threads are ready before we leave -- so that
	// both threads have a copy of pingpong_mutex.
	pthread_mutex_lock(args->mutex);
    if (*(args->flag)) {
        *(args->flag) = 0;
        pthread_cond_wait(args->cond, args->mutex);
    } else {
        *(args->flag) = 1;
        pthread_cond_broadcast(args->cond);
    }
    pthread_mutex_unlock(args->mutex);
	*(args->prepared) = true;
}

static void *thread_fn(void *data)
{
//	moveCurrentThread();
	int amount_of_loops = 0;
	thread_args_t *args = (thread_args_t *)data;
	common_setup(args);
	big_atomic_t *nr_pingpongs = args->nr_pingpongs;
	atomic_t nr = 0;
	bool done = false;
	atomic_t me = args->me;
	atomic_t buddy = args->buddy;
	int *stop_loops = args->stoploops;
	int *max_loops = args->max_loops;
	atomic_t *cache_pingpong_mutex = *(args->pingpong_mutex);
	while (1) {
		if(amount_of_loops++ >  *max_loops || (args->timestamps).size() > act_sample){
			//if(amount_f_loops > *max_loops*2){
			//	pthread_exit(0);
			//}
			if(*stop_loops == 1){
				*stop_loops +=3;
				pthread_exit(0);
			}else{
			   *stop_loops += 1;
			}
		}
		if (*stop_loops>2){
			pthread_exit(0);
		}

		if (__sync_bool_compare_and_swap(cache_pingpong_mutex, me, buddy)) {
			++nr;
			if ((nr>nr_param) && me == 0) {
				(args->timestamps).push_back(now_nsec());
				nr = 0;
			}
		}
		//(args->timestamps).push_back(now_nsec());
		//nr=0;

	}
	return NULL;
}

//pins calling thread to two cores
int stick_this_thread_to_core(int core_id,int core_id2) {
   int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
   if (core_id < 0 || core_id >= num_cores)
      return EINVAL;
   cpu_set_t cpuset;
   CPU_ZERO(&cpuset);
   CPU_SET(core_id, &cpuset);
   CPU_SET(core_id2, &cpuset);
   pthread_t current_thread = pthread_self();    
   return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

int get_latency_class(int latency){
        if(latency<0 || latency>90000){
                return 1;
        }

        if(latency< 2000){
                return 2;
        }
        if(latency< threefour_latency_class){
                return 3;
        }

        return 4;
}




int measure_latency_pair(int i, int j)
{

	if(vcap_banned[i] || vcap_banned[j]){
		return threefour_latency_class + 30;
	}
	int sleeping_time = SAMPLE_US;
	int amount_of_times=0;
	if(latency_valid != -1 && latency_valid != 1){
                        amount_of_times = -2;
    }
	if(latency_valid == 1){
		amount_of_times = 2;
	}
	int max_loops = SAMPLE_US;
	if(first_measurement){
		amount_of_times = -2;
		max_loops = SAMPLE_US * 10;
		first_measurement = false;
	}
	
	
	while(1){
		stick_this_thread_to_core(i,j);
		atomic_t* pingpong_mutex = (atomic_t*) malloc(sizeof(atomic_t));;
		pthread_mutex_t wait_mutex = PTHREAD_MUTEX_INITIALIZER;
		pthread_cond_t wait_cond = PTHREAD_COND_INITIALIZER;
		big_atomic_t nr_pingpongs;
		int stop_loops = 0;
		bool prepared = false;
		int wait_for_buddy = 1;
		thread_args_t even(i, (atomic_t)0, (atomic_t)1, &pingpong_mutex, &nr_pingpongs, &stop_loops, &wait_mutex, &wait_cond, &wait_for_buddy,&prepared,&max_loops);
		thread_args_t odd(j, (atomic_t)1, (atomic_t)0, &pingpong_mutex, &nr_pingpongs, &stop_loops, &wait_mutex, &wait_cond, &wait_for_buddy,&prepared,&max_loops);
		pthread_t t_odd;
		pthread_t t_even;
		__sync_lock_test_and_set(&nr_pingpongs.x, 0);


		if (pthread_create(&t_odd, NULL, thread_fn, &odd)) {
			printf("ERROR creating odd thread\n");
			exit(1);
		}
		if (pthread_create(&t_even, NULL, thread_fn, &even)) {
			printf("ERROR creating even thread\n");
			exit(1);
		}

		double best_sample = 1./0.;
		
		pthread_join(t_odd,NULL);
		pthread_join(t_even,NULL);
		if(even.timestamps.size() == 1){
					continue;
		}
		munmap(pingpong_mutex,getpagesize());

		if(even.timestamps.size() < 2){
			if(amount_of_times<NR_SAMPLES){
				amount_of_times++;
				//max_loops = SAMPLE_US * 2;
				continue;
			}else{
				atomic_t s = __sync_lock_test_and_set(&nr_pingpongs.x, 0);
				if(verbose){
					std::cout <<"Times around:"<<amount_of_times<<"I"<<i<<" J:"<<j<<" Sample passed " << -1 << " next.\n";
				}
				return -1;
			}
		}
		 if(even.timestamps.size() < (act_sample-1)){
                                        all_samples_found = false;
                                }

		for(int z=0;z<even.timestamps.size() - 1;z++){
			double sample = (even.timestamps[z+1] - even.timestamps[z]) / (double)(nr_param*2);
			if (sample < best_sample){
				best_sample = sample;
			}
		}
	  //if(even.timestamps.size()<2){
	//	return -1;
	//}

//	if(abs(threefour_latency_class - (best_sample * 100)) < 400){
//		std::cout<<"threshold adjusted"<<std::endl;
//		threefour_latency_class = threefour_latency_class*1;
//	}
		if(verbose){
		std::cout<<"Times around:"<<amount_of_times<<"I"<<i<<" J:"<<j<<" Sample passed " << (int)(best_sample*100) << " next.\n";
		}
		return (int)(best_sample * 100);
	}
}






void set_latency_pair(int x,int y,int latency_class){
	top_stack[x][y] = latency_class;
	top_stack[y][x] = latency_class;
}

void apply_optimization(void){
	int sub_rel;
	for(int x=0;x<LAST_CPU_ID;x++){
		for(int y=0;y<LAST_CPU_ID;y++){
			sub_rel = top_stack[y][x];
			for(int z=0;z<LAST_CPU_ID;z++){
				if((top_stack[y][z]<sub_rel && top_stack[y][z]!=0)){
					

					if(top_stack[x][z] == 0){
						set_latency_pair(x,z,sub_rel);
					}else if(top_stack[x][z] != sub_rel){
						failed_test = true;
						return;
					}
					
				}
			}
		}
	}
}




static void print_population_matrix(void)
{
	int i, j;

	for (i = 0; i < LAST_CPU_ID; i++) {
		for (j = 0; j < LAST_CPU_ID; j++)
			if((int)(top_stack[i][j]) == -1){
			printf("%7s", "INF");
			}else{
			printf("%7d", (int)(top_stack[i][j]));
			}
		printf("\n");
	}
}





int find_numa_groups(void)
{
	nr_numa_groups = 0;
	for(int i = 0;i<LAST_CPU_ID;i++){
		cpu_group_id[i] = -1;
	}
	numa_to_pair_arr = {};
	numas_to_cpu = {};
	first_measurement = true;
	for (int i = 0; i < LAST_CPU_ID; i++) {
		if(cpu_group_id[i] != -1){
			continue;
		}
		cpu_group_id[i] = nr_numa_groups;
		for (int j = 0; j < LAST_CPU_ID; j++) {
			if(cpu_group_id[j] != -1){
				continue;
			}
			if(top_stack[i][j] == 0 ){
				int latency = measure_latency_pair(i,j);
				set_latency_pair(i,j,get_latency_class(latency));
			}
			if(top_stack[i][j] < 4){
				cpu_group_id[j] = nr_numa_groups;
			}
		}
		nr_numa_groups++;
		std::vector<int> cpu_bitmap_group(LAST_CPU_ID);
		numa_to_pair_arr.push_back(cpu_bitmap_group);
		numas_to_cpu.push_back(i);
	}

	apply_optimization();
	return nr_numa_groups;
}

typedef struct {
	std::vector<int> pairs_to_test;
} worker_thread_args;


void ST_find_topology(std::vector<int> input){
	for(int x=0;x<input.size();x++){
		int j = input[x] % LAST_CPU_ID;
		int i = (input[x]-(input[x]%LAST_CPU_ID))/LAST_CPU_ID;
		
		
		if(top_stack[i][j] == 0){
			int latency = measure_latency_pair(i,j);
			pthread_mutex_lock(&top_stack_mutex);
			set_latency_pair(i,j,get_latency_class(latency));
			if(latency_valid == -1){
				apply_optimization();
			}
			pthread_mutex_unlock(&top_stack_mutex);
		}
		if(failed_test || (latency_valid != -1 && latency_valid != top_stack[i][j])){
			failed_test = true;
			return;
		}
		
	}
	return;	
}



static void *thread_fn2(void *data)
{
	
	worker_thread_args *args = (worker_thread_args *)data;
	ST_find_topology(args->pairs_to_test);
	return NULL;
}


void MT_find_topology(std::vector<std::vector<int>> all_pairs_to_test){ 

	worker_thread_args worker_args[all_pairs_to_test.size()];
	pthread_t worker_tasks[all_pairs_to_test.size()];
	
	for (int i = 0; i < all_pairs_to_test.size(); i++) {
		worker_args[i].pairs_to_test = all_pairs_to_test[i];
		pthread_create(&worker_tasks[i], NULL, thread_fn2, &worker_args[i]);
	}
	for (int i = 0; i < all_pairs_to_test.size(); i++) {
    		pthread_join(worker_tasks[i], NULL);
  	}
}

void performProbing(){
	failed_test = false;
	all_samples_found = true;
	updateVectorFromBanlist(banlistPath / "vcap_strag.txt");
	find_numa_groups();
	apply_optimization();
	std::vector<std::vector<int>> all_pairs_to_test(nr_numa_groups);
	for(int i=0;i<LAST_CPU_ID;i++){
		for(int j=i+1;j<LAST_CPU_ID;j++){
			if(top_stack[i][j] == 0){
				if(cpu_group_id[i] == cpu_group_id[j]){
					all_pairs_to_test[cpu_group_id[i]].push_back(i * LAST_CPU_ID + j);
				}
			}
		}
	}
	MT_find_topology(all_pairs_to_test);
}



bool verify_numa_group(std::vector<int> input){
	std::vector<int> nums;
	for (int i = 0; i < input.size(); ++i) {
        	if (input[i] == 1) {
            		nums.push_back(i);
        	}
    	}
	for(int i=0; i < nums.size();i++){
		for(int j=i+1;j<nums.size();j++){
			int latency = measure_latency_pair(pairs_to_cpu[nums[i]],pairs_to_cpu[nums[j]]);
			if(get_latency_class(latency) != 3){
				return false;
			}
		}
	}
	return true;
}

std::vector<int> bitmap_to_ord_vector(std::vector<int> input){
	std::vector<int> ord_vector;
	for(int i=0;i<input.size();i++){
                if(input[i] == 1){
                    ord_vector.push_back(i);
                }
        }
	return ord_vector;

}


std::vector<int> bitmap_to_task_stack(std::vector<int> input,int type){
	std::vector<int> stack;
	std::vector<int> returnstack;
	for(int i=0;i<input.size();i++){
		if(input[i] == 1){
			if(type == NUMA_GROUP){
				stack.push_back(pairs_to_cpu[i]);
			}else if(type == PAIR_GROUP){
				stack.push_back(threads_to_cpu[i]);
			}else{
				stack.push_back(i);
			}
		}
	}
	for(int i=0;i<stack.size();i++){
		for(int j=i+1;j<stack.size();j++){
			returnstack.push_back(stack[i]*LAST_CPU_ID+stack[j]);
		}
	}
	return returnstack;
}





void nullify_changes(std::vector<std::vector<int>> input){
	for (int z = 0; z < input.size(); z++) {
		for (int x = 0; x < input[z].size();x++) {
			int j = input[z][x] % LAST_CPU_ID;
			int i = (input[z][x]-(input[z][x]%LAST_CPU_ID))/LAST_CPU_ID;
			set_latency_pair(i,j,0);
		}
	}

}


bool verify_topology(void){
	for(int i=0;i<LAST_CPU_ID;i++){
		for(int j=0;j<LAST_CPU_ID;j++){
			if(i==j){
				top_stack[i][j] = 1;
			}else{
				top_stack[i][j] = 0;
			}
		}
	}
	first_measurement = true;
	for(int i=0;i<nr_numa_groups;i++){
        	for(int j=i+1;j<nr_numa_groups;j++){
			int latency = measure_latency_pair(numas_to_cpu[i],numas_to_cpu[j]);
                	if(get_latency_class(latency) != 4){
                        	return false;
                	}
		}
    }

	std::vector<std::vector<int>> task_set_arr(numa_to_pair_arr.size());
	for(int i=0;i<numa_to_pair_arr.size();i++){
		task_set_arr[i] = bitmap_to_task_stack(numa_to_pair_arr[i],NUMA_GROUP);
	}
	latency_valid = 3;
	MT_find_topology(task_set_arr);
	if(failed_test == true){
		nullify_changes(task_set_arr);
		return false;
	}
	task_set_arr = std::vector<std::vector<int>>(pair_to_thread_arr.size());
	for(int i=0;i<pair_to_thread_arr.size();i++){
		task_set_arr[i] = bitmap_to_task_stack(pair_to_thread_arr[i],PAIR_GROUP);
	}
	latency_valid = 2;
	MT_find_topology(task_set_arr);
	
	if(failed_test == true){
		nullify_changes(task_set_arr);
		return false;
	}
	task_set_arr = std::vector<std::vector<int>>(pair_to_thread_arr.size()); 
	for(int i=0;i<pair_to_thread_arr.size();i++){
		std::vector<int> threads_in_pair = bitmap_to_ord_vector(pair_to_thread_arr[i]);
		for(int g=0;g<threads_in_pair.size();g++){
			int thread = threads_in_pair[g];
			std::vector<int> cpus_in_thread = bitmap_to_ord_vector(thread_to_cpu_arr[thread]);
			for(int f=0;f<cpus_in_thread.size()-1;f++){
				int i_value =  cpus_in_thread[f];
				int j_value = cpus_in_thread[f+1];
				task_set_arr[i].push_back(i_value*LAST_CPU_ID+j_value);
			}
		}
	}
	latency_valid = 1;
	MT_find_topology(task_set_arr);
	if(failed_test == true){
		nullify_changes(task_set_arr);
		return false;
	}
	return true;
}


//TODO rename, parse matrix
static void parseTopology(void)
{
	int i, j, count = 0;
	nr_pair_groups = 0;
	nr_tt_groups = 0;
	nr_cpus = get_nprocs();


	//clear all previous topology data(excluding numa level)
	for (i = 0; i < LAST_CPU_ID; i++){
		cpu_pair_id[i] = -1;
		cpu_tt_id[i] = -1;
	}
	pair_to_thread_arr={};
	thread_to_cpu_arr={};
	pairs_to_cpu={};
	threads_to_cpu={};


	for (i = 0; i < LAST_CPU_ID; i++) {
		if (cpu_pair_id[i] == -1){
			cpu_pair_id[i] = nr_pair_groups;
			nr_pair_groups++;
			std::vector<int> cpu_bitmap_pair(LAST_CPU_ID);
			pair_to_thread_arr.push_back(cpu_bitmap_pair);
			pairs_to_cpu.push_back(i);
		}
		
		if (cpu_tt_id[i] == -1){
			cpu_tt_id[i] = nr_tt_groups;
			nr_tt_groups++;
			std::vector<int> cpu_bitmap_tt(LAST_CPU_ID);
            		thread_to_cpu_arr.push_back(cpu_bitmap_tt);
			threads_to_cpu.push_back(i);
		}

		for (j = 0 ; j < LAST_CPU_ID; j++) {
				if (top_stack[i][j]<3 && cpu_pair_id[i] != -1){
					cpu_pair_id[j] = cpu_pair_id[i];
				}
				if (top_stack[i][j]<2 && cpu_tt_id[i] != -1){
					cpu_tt_id[j] = cpu_tt_id[i];
				}

		}
		numa_to_pair_arr[cpu_group_id[i]][cpu_pair_id[i]] = 1;
		pair_to_thread_arr[cpu_pair_id[i]][cpu_tt_id[i]] = 1;
		thread_to_cpu_arr[cpu_tt_id[i]][i] = 1;
	}
	int spaces = 0;
	for (int i = 0; i < nr_numa_groups; i++) {
		spaces=0;
		std::vector<int> pairs_in_numa =  bitmap_to_ord_vector(numa_to_pair_arr[i]);
		for(int j = 0;j<pairs_in_numa.size();j++){
			std::vector<int> threads_in_pair = bitmap_to_ord_vector(pair_to_thread_arr[pairs_in_numa[j]]);	
			for(int z=0;z<threads_in_pair.size();z++){
				std::vector<int> cpus_in_thread = bitmap_to_ord_vector(thread_to_cpu_arr[threads_in_pair[z]]); 
				spaces+=1;
				for(int y=0;y<cpus_in_thread.size();y++){
					spaces+=3;
				}
			}
		}
		std::cout<<"[";
		for(int l = 0; l<spaces-2;l++){
			std::cout<<" ";
		}
		std::cout<<"]";
	}
	printf("\n");
	spaces = 0;

	for (int i = 0; i < nr_numa_groups; i++) {
                spaces=0;
                std::vector<int> pairs_in_numa =  bitmap_to_ord_vector(numa_to_pair_arr[i]);
                for(int j = 0;j<pairs_in_numa.size();j++){
                        std::vector<int> threads_in_pair = bitmap_to_ord_vector(pair_to_thread_arr[pairs_in_numa[j]]); 
                        for(int z=0;z<threads_in_pair.size();z++){
                                std::vector<int> cpus_in_thread = bitmap_to_ord_vector(thread_to_cpu_arr[threads_in_pair[z]]); 
                                spaces+=1;
                                for(int y=0;y<cpus_in_thread.size();y++){
                                        spaces+=3;
                                }
                        }
			std::cout<<"[";
                	for(int l = 0; l<spaces-2;l++){
                        	std::cout<<" ";
                	}
                	std::cout<<"]";
			spaces=0;
                }
        }
        printf("\n");
	for (int i = 0; i < nr_numa_groups; i++) {
                spaces=0;
                std::vector<int> pairs_in_numa =  bitmap_to_ord_vector(numa_to_pair_arr[i]);
                for(int j = 0;j<pairs_in_numa.size();j++){
                        std::vector<int> threads_in_pair = bitmap_to_ord_vector(pair_to_thread_arr[pairs_in_numa[j]]); 
                        for(int z=0;z<threads_in_pair.size();z++){
                                std::vector<int> cpus_in_thread = bitmap_to_ord_vector(thread_to_cpu_arr[threads_in_pair[z]]); 
                                std::cout<<"[";
                                for(int y=0;y<cpus_in_thread.size();y++){
					printf("%2d",cpus_in_thread[y]);
					if(y!=cpus_in_thread.size()-1){
						std::cout<<" ";
					}
                                }
                                std::cout<<"]";
                        }
                }
        }
        printf("\n");

	printf("%d ", nr_numa_groups);	
	printf("%d ", nr_pair_groups);	
	printf("%d ", nr_tt_groups);	
	printf("\n");
}

#define CPU_ID_SHIFT		(32)
/*
 * %4 is specific to our platform.
 */
#define CPU_NUMA_GROUP(mode, i)	(mode == PROBE_MODE ? cpu_group_id[i] : i % 4)
static void configure_os_numa_groups(int mode)
{
	int i;
	unsigned long val;

	/*
	 * pass vcpu & numa group id in a single word using a simple encoding:
	 * first 16 bits store the cpu identifier
	 * next 16 bits store the numa group identifier
	 * */
	for(i = 0; i < LAST_CPU_ID; i++) {
		/* store cpu identifier and left shift */
		val = i;
		val = val << CPU_ID_SHIFT;
		/* store the numa group identifier*/
		val |= CPU_NUMA_GROUP(mode, i);
	}
}

void resetTopologyMatrix(){
	for (int i = 0; i < LAST_CPU_ID; i++) {
		for(int p=0;p< LAST_CPU_ID;p++){
			if(p!=i){
				top_stack[i][p] = 0;
			}
		}
	}
}



bool performProbingMultiple(std::vector<std::vector<int>> copy_top_stack){
        printf("new test\n");
	bool inner_failed_test = 0;
	for(int i = 0;i<4;i++){
		failed_test = false;
		performProbing();
		if(!failed_test){
			parseTopology();
		}else{
	        printf("simple failure\n");
	 	}
		if(failed_test || (copy_top_stack != top_stack) ){
			printf("Failed above test\n");
			inner_failed_test = 1;
		}
		resetTopologyMatrix();
		sleep(1);
	}
	return !(inner_failed_test);
}


void performParameterSearch(){
	int lower_bound = 1;
	int upper_bound = 1000;
	act_sample = 1000;
	SAMPLE_US = 90000;
	performProbing();
	printf("Example probing done - results\n");
	parseTopology();
	std::vector<std::vector<int>> copy_top_stack;

	std::vector<std::vector<int>> copy_numa_to_pair_arr;
	std::vector<std::vector<int>> copy_pair_to_thread_arr;
	std::vector<std::vector<int>> copy_thread_to_cpu_arr;

	copy_numa_to_pair_arr.assign(numa_to_pair_arr.begin(),numa_to_pair_arr.end());
	copy_pair_to_thread_arr.assign(pair_to_thread_arr.begin(),pair_to_thread_arr.end());
	copy_thread_to_cpu_arr.assign(thread_to_cpu_arr.begin(),thread_to_cpu_arr.end());

	copy_top_stack.assign(top_stack.begin(),top_stack.end());
	while(true){
		bool small_passed = 0;
		bool large_passed = 0;
		int test_point = (int)(upper_bound+lower_bound) / 2;
		int test_point_large = (int)(test_point * 1.5);

		//Perform probing on test point
		act_sample = test_point;
		small_passed = performProbingMultiple(copy_top_stack);
		//we're aiming such that larger one passes and smaller does not - if both pass, 
		//then we must move lower - if both fail, we move higher.
		if(upper_bound < lower_bound*1.3){
			printf("correct sample value found\n");
                        printf("Found number of samples:%d \n", upper_bound);
                        break;
		}

		if(small_passed){
			printf("value too high %d",test_point);
			upper_bound = test_point;
		}else{
			printf("value too low %d",test_point);
			lower_bound = test_point;
		}
	}
	act_sample = (int)  (1.5 *  upper_bound);
	printf("Set number of samples:%d \n", act_sample);
	lower_bound = 1;
	upper_bound = 90000;
	while(true){
                bool small_passed = 0;
                int test_point = (int)(upper_bound+lower_bound) / 2;
                int test_point_large = (int)(test_point * 1.5);

                //Perform probing on test point
                SAMPLE_US = test_point;
                small_passed = performProbingMultiple(copy_top_stack);
                //we're aiming such that larger one passes and smaller does not - if both pass, 
                //then we must move lower - if both fail, we move higher.
                if(upper_bound < lower_bound*1.3){
                        printf("correct sample value found\n");
                        printf("Found number of samples:%d \n", upper_bound);
                        break;
                }

                if(small_passed){
                        printf("value too high %d",test_point);
                        upper_bound = test_point;
                }else{
                        printf("value too low %d",test_point);
                        lower_bound = test_point;
                }
        }
        SAMPLE_US = (int)(1.5 * upper_bound);
	printf("Set number of tries:%d \n", SAMPLE_US);




}

int main(int argc, char *argv[])
{
	
	uint64_t popul_laten_last = now_nsec();
	uint64_t popul_laten_now = now_nsec();
	//set program to high priority
	moveCurrentThread();
	nr_cpus = get_nprocs();

	//initialize the topology matrix
	for (int i = 0; i < LAST_CPU_ID; i++) {
		std::vector<int> cpumap(LAST_CPU_ID);
		top_stack.push_back(cpumap);
	}
	for(int p=0;p< LAST_CPU_ID;p++){
		top_stack[p][p] = 1;
	}
	for(int p=0;p< LAST_CPU_ID;p++){
                vcap_banned.push_back(0);
        }
	
	const std::vector<std::string_view> args(argv, argv + argc);
  	setArguments(args);
	//first time probing
	//enableAllCpus();
	//performParameterSearch();
	performProbing();
	if(!failed_test){
		giveTopologyToKernel();
		parseTopology();
		disableStackingCpus();
	}else{
		//std::fill(vcap_banned.begin(), vcap_banned.end(), 0);
		printf("Probing failed, waiting until next session\n");
	}
	while(1){
		if(verbose){
			//print_population_matrix();
		}
		popul_laten_last = now_nsec();
		if(!failed_test){
			bool topology_passed = verify_topology();
			
			latency_valid = -1;
			if (topology_passed){
				popul_laten_now = now_nsec();
				printf("TOPOLOGY VERIFIED.TOOK (MILLISECONDS):%lf\n", (popul_laten_now-popul_laten_last)/(double)1000000);
				disableStackingCpus();
			}else{
				popul_laten_now = now_nsec();
				printf("TOPOLOGY FAILED.TOOK (MILLISECONDS):%lf\n", (popul_laten_now-popul_laten_last)/(double)1000000);
				popul_laten_last = now_nsec();
				performProbing();
				if(!failed_test){
					giveTopologyToKernel();
					parseTopology();
					disableStackingCpus();
				}else{
					//enableAllCpus();
					printf("Probing failed, waiting until next session\n");
					resetTopologyMatrix();
				//	std::fill(vcap_banned.begin(), vcap_banned.end(), 0);
				}
				popul_laten_now = now_nsec();
				printf("REPROBING.TOOK (MILLISECONDS):%lf\n", (popul_laten_now-popul_laten_last)/(double)1000000);
			}
		}else{
			
			performProbing();
			if(!failed_test){
				giveTopologyToKernel();
				parseTopology();
				disableStackingCpus();

			}else{
				//enableAllCpus();
				printf("Probing failed, waiting until next session\n");
				//std::fill(vcap_banned.begin(), vcap_banned.end(), 0);
				resetTopologyMatrix();
			}
		}
		printf("Done...\n");
		sleep(sleep_time);
	}
}

