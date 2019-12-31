#include "runformation.h"

struct RunformTime run_time;

static unsigned long long * __time_alloc(int nr){
	return (unsigned long long *)calloc(nr, sizeof(unsigned long long));
}

static void time_init(int nr_thread, struct RunformTime *time){
	time->runform_t = 0;
	time->runform_c = 0;
	time->runform_arrival_t = __time_alloc(nr_thread);
	time->runform_arrival_c = __time_alloc(nr_thread);
	time->runform_read_t = __time_alloc(nr_thread);
	time->runform_read_c = __time_alloc(nr_thread);
	time->runform_write_t = __time_alloc(nr_thread);
	time->runform_write_c = __time_alloc(nr_thread);
	time->runform_sort_t = __time_alloc(nr_thread);
	time->runform_sort_c = __time_alloc(nr_thread);
}


static struct Data* alloc_buf(int64_t size){
	void *mem;

	posix_memalign(&mem, 4096, size);
	Data *tmp = new (mem) Data;

	return tmp;
}

static bool 
compare(struct Data a, struct Data b){
	return  (a.key < b.key);
}

static void 
range_balancing(struct Data* buf, int nr_range, int64_t size, 
				int run, uint64_t *range_table)
{
	int range = 0;
	int nr_filter = nr_range * nr_range;
	int filter = MAXKEY/nr_filter;

	uint64_t *buf_filter;
	buf_filter = (uint64_t*)calloc(nr_filter, sizeof(uint64_t));
	assert(buf_filter != NULL);

	int64_t nr_entries = size/KV_SIZE;

	for(int i = 0; i < nr_entries; i++){
		*(buf_filter + buf[i].key/filter) += 1;

	}

	if(nr_range > 16){
		for(int j = 0; j < nr_filter; j++){
			range_table[run*nr_range + range] += buf_filter[j];
			if((range_table[run*nr_range + range] > nr_entries/nr_range) && 
					(range < nr_range - 1))
			{
				range++;
			}
		}
	}
	else {
		for(int j = 0; j < NR_FILTER; j++){
			range_table[run*nr_range + range] += buf_filter[j];
			if((range_table[run*nr_range + range] > nr_entries/(nr_range + 1)) && 
					(range < nr_range-1))
			{
				range++;
			}
		}
	}
	free(buf_filter);
}

static void
reverse_table(uint64_t *table, int row, int col){
	/* implemented in a naive way */
	uint64_t *new_table;
	new_table = (uint64_t *)malloc(row * col * sizeof(uint64_t));

	for(int i = 0; i < row; i++){
		/* data moved from row to column */
		for(int j = 0; j < col; j++){
			new_table[j * col + i] = table[i * row + j];
		}
	}

	memcpy(table, new_table, col * row * sizeof(uint64_t));	
	free(new_table);
}


static void
print_range(uint64_t *range_table, int nr_range, int nr_run){
	uint64_t range_sum;
	for(int range = 0; range < nr_range; range++){
		range_sum = 0;
		for(int run = 0; run < nr_run; run++){
			range_sum += range_table[range * nr_run + run];
		}
		std::cout << "range[" << range << "]: " << range_sum << std::endl;
	}
}

static void 
range_to_file(uint64_t *range_table, struct opt_t odb){
	
	reverse_table(&range_table[0], odb.nr_merge_th, odb.nr_run);
	print_range(&range_table[0], odb.nr_merge_th, odb.nr_run);
	
	FILE* meta = fopen(odb.metapath.c_str(), "w+");
	
	fwrite(&range_table[0], sizeof(uint64_t), odb.nr_merge_th * odb.nr_run, meta);
	
	fclose(meta);
}

static void * 
t_RunFormation(void *data){
	struct RunformationArgs args = *(struct RunformationArgs*)data;
	int fd_input = args.fd_input;
	int nr_run = args.nr_run;
	int nr_range = args.nr_range;
	int th_id = args.th_id;
	int data_size = args.data_size;
	int blk_size = args.blk_size;
	off_t offset = args.offset;
	std::atomic<int> *run_id = args.run_id;
	std::string runpath = args.runpath;
	
	uint64_t *range_table = args.range_table;
	Data *runbuf = alloc_buf(blk_size);

	int64_t nbyte_read = 0;
	
	struct timespec local_time[2];
	struct timespec thread_time[2];
#if DO_PROFILE
	clock_gettime(CLOCK_MONOTONIC, &thread_time[0]);
#endif

	while(nbyte_read < data_size){
		int run = run_id->fetch_add(1);
#if DO_PROFILE
		clock_gettime(CLOCK_MONOTONIC, &local_time[0]);
#endif
		int64_t tmp_read = read(fd_input, (char*)runbuf, blk_size);
		assert(tmp_read == blk_size);

		nbyte_read += tmp_read;		
#if DO_PROFILE
		clock_gettime(CLOCK_MONOTONIC, &local_time[1]);
		calclock(local_time, &run_time.runform_read_t[th_id], &run_time.runform_read_c[th_id]);
#endif

		/* Sort (STL) */	
		std::sort(&runbuf[0], &runbuf[0] + blk_size/KV_SIZE, compare);

		/* gather range statistics from sorted buffer */		
		range_balancing(runbuf, nr_range, blk_size, run, range_table);

		int fd_run = open( (runpath + std::to_string(run)).c_str(),
			       	O_DIRECT | O_RDWR | O_CREAT | O_LARGEFILE, 0644);
		assert(fd_run > 0);
	
#if DO_PROFILE
		clock_gettime(CLOCK_MONOTONIC, &local_time[0]);
#endif
	        int64_t tmp_write = write(fd_run, (char *)&runbuf[0], blk_size);
		assert(tmp_write == blk_size);
#if DO_PROFILE
		clock_gettime(CLOCK_MONOTONIC, &local_time[1]);
		calclock(local_time, &run_time.runform_write_t[th_id], &run_time.runform_write_c[th_id]);
#endif
		close(fd_run);
	}

#if DO_PROFILE
	clock_gettime(CLOCK_MONOTONIC, &thread_time[1]);
	calclock(thread_time, &run_time.runform_arrival_t[th_id], &run_time.runform_arrival_c[th_id]);
	run_time.runform_sort_t[th_id] = run_time.runform_arrival_t[th_id] - run_time.runform_read_t[th_id] -
							run_time.runform_write_t[th_id];
#endif
	free(runbuf);

	return ((void *)1);
}

void 
RunFormation(void* data){
	struct opt_t odb = *(struct opt_t *)data;
	
	std::atomic<int> run_id = { 0 };
	uint64_t *range_table;
	range_table = (uint64_t *)calloc(odb.nr_runform_th * odb.nr_merge_th * 
						odb.nr_run, sizeof(uint64_t));

	struct RunformationArgs runformation_args[odb.nr_runform_th];
	int thread_id[odb.nr_runform_th];
	pthread_t p_thread[odb.nr_runform_th];
	
	time_init(odb.nr_runform_th, &run_time);	
	int fd_input = open( odb.inpath.c_str(), O_DIRECT | O_RDONLY);
	assert(fd_input > 0);
	
	off_t cumulative_offset = 0;
	for(int th_id = 0; th_id < odb.nr_runform_th; th_id++){
		runformation_args[th_id].fd_input = fd_input;
		runformation_args[th_id].th_id = th_id;
		runformation_args[th_id].nr_run = odb.nr_run;
		runformation_args[th_id].nr_range = odb.nr_merge_th;
		runformation_args[th_id].data_size = odb.total_size/odb.nr_runform_th;
		runformation_args[th_id].blk_size = odb.rf_blksize;
		runformation_args[th_id].offset = (odb.total_size/odb.nr_runform_th)*th_id;
		runformation_args[th_id].run_id = &run_id;
		runformation_args[th_id].runpath = odb.runpath;
		runformation_args[th_id].range_table = range_table + 
					(th_id * odb.nr_merge_th * odb.nr_run);

		thread_id[th_id] = pthread_create(&p_thread[th_id], NULL, 
				t_RunFormation, (void*)&runformation_args[th_id]);
	}
	
	int is_ok = 0;
	for(int th_id = 0; th_id < odb.nr_runform_th; th_id++){
		pthread_join(p_thread[th_id], (void**)&is_ok);
		assert(is_ok == 1);
	}
	
	range_to_file(&range_table[0], odb);

	close(fd_input);
	free(range_table);
}

