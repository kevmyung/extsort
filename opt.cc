#include <stdio.h>
#include <algorithm>
#include <stdlib.h>

#include <assert.h>
#include "opt.h"

int do_profile;
int do_verify;
int do_clear;

void
opt_init(struct opt_t *odb)				/* option database */
{

	odb->datagen = DO_DATAGEN;
	odb->runform = DO_RUNFORM;
	odb->merge = DO_MERGE;
	do_verify = DO_VERIFY;
	do_profile = DO_PROFILE;
	do_clear = 1;
	odb->inpath = INPUT_PATH;			/* path to input file */
	odb->outpath = OUTPUT_PATH;			/* path to output files */
	odb->outpath += "range_";
	odb->runpath = RUN_PATH;			/* path to temporary run files */
	odb->runpath += "run_";
	odb->metapath = META_PATH;			/* path to temporary range metadata */
	odb->nr_datagen_th = NRTH_DATAGEN;		/* number of data generation threads */
	odb->nr_runform_th = NRTH_RUNFORM;		/* number of runformation threads */
	odb->nr_merge_th = NRTH_MRG;			/* number of merge threads */
	odb->total_size = TOTAL_DATA_SIZE;		/* total file size */
	odb->mem_size = MEM_SIZE;			/* total memory size */
	odb->nr_run = 					/* number of run files */
		(odb->total_size/odb->mem_size)
				*odb->nr_runform_th;

	assert(odb->nr_run > 0);

	odb->kv_size = KV_SIZE;				/* key+value size */
	odb->key_size = /*8B*/ 8;			/* key size */
	odb->rf_blksize = 				/* available blkbuf per runformation thread */
		(odb->mem_size/odb->nr_runform_th);

	assert(odb->rf_blksize >= 4096);

	odb->mrg_blksize =				/* available blkbuf per run file */
		(odb->mem_size/odb->nr_merge_th
		 /odb->nr_run);

	assert(odb->mrg_blksize >= 4096);

	odb->mrg_wrbuf = /*1MB*/ 1*1024*1024;		/* available write buffer per merge thread */
}


static std::string
char_to_str(char* a, int size){
	int i;
	std::string s = "";
	for (i = 0; i < size; i++){
		s += a[i];
	}
	return s;
}

static void
usage(){
	std::cout << "\n    [  Parallel External Sorting  ]" << std::endl;
	std::cout << "\n	< USAGE: " ;
	std::cout << " ./extsort <options> > \n" << std::endl;
	std::cout << "	-h        | Usage" << std::endl;
	std::cout << "	-G        | Generate a new data file (default: false)" << std::endl;
	std::cout << "	-R        | Do run formation (default: false)" << std::endl;
	std::cout << "	-M        | Do merge (default: false)" << std::endl;
	std::cout << "	-P        | Profile the overall performance (default: false)" << std::endl;
	std::cout << "	-V        | Verify during the procedures (default: false)" << std::endl;
	std::cout << "	-c  <opt> | Clear files after test (default: 1)" << std::endl;
	std::cout << "                  | (0: none / 1: run / 2: run+input)" << std::endl;
	std::cout << "	-t  <opt> | Metadata(=range table) path" << std::endl;
	std::cout << "	-i  <opt> | Input file path" << std::endl;
	std::cout << "	-o  <opt> | Output file path" << std::endl;
	std::cout << "	-r  <opt> | Run file path" << std::endl;
	std::cout << "	-d  <opt> | Data(input) file size to be sorted (default: 1GB)" << std::endl;
	std::cout << "	-m  <opt> | Memory capacity to be used (default: 128MB)" << std::endl;
	std::cout << "	-t  <opt> | Number of threads to be used (default: 4)\n" << std::endl;
}

static char*
get_option(char **begin, char **end, const std::string &option){
	char ** itr = std::find(begin, end, option);
	if (itr != end && ++itr != end){
		return *itr;
	}
	return 0;
}

static bool
set_flag(char **begin, char **end, const std::string &option){
	char ** itr = std::find(begin, end, option);
	if (itr != end){
		return 1;
	}
	return 0;
}

void
opt_parse(int argc, char *argv[], struct opt_t *odb){
	bool hlp = set_flag(argv, argv + argc, "-h");
	bool gen = set_flag(argv, argv + argc, "-G");
	bool rnf = set_flag(argv, argv + argc, "-R");
	bool mrg = set_flag(argv, argv + argc, "-M");
	bool prf = set_flag(argv, argv + argc, "-P");
	bool vrf = set_flag(argv, argv + argc, "-V");
	char *clr = get_option(argv, argv + argc, "-C");
	char *tablepath = get_option(argv, argv + argc, "-t");
	char *inpath = get_option(argv, argv + argc, "-i");
	char *outpath = get_option(argv, argv + argc, "-o");
	char *runpath = get_option(argv, argv + argc, "-r");
	char *datasize = get_option(argv, argv + argc, "-d");
	char *memsize = get_option(argv, argv + argc, "-m");
	char *th = get_option(argv, argv + argc, "-w");

	if(hlp){
		usage();
		exit(1);
	}
	if(gen){
		odb->datagen = true;
	}
	if(rnf){
		odb->runform = true;
	}
	if(mrg){
		odb->merge = true;
	}
	if(vrf){
		do_verify = true;
	}
	if(prf){
		do_profile = true;
	}
	if(clr){
		do_clear = atoi(clr);
	}
	if(tablepath){
		odb->metapath = char_to_str(tablepath, sizeof(tablepath)/sizeof(char));
	}
	if(inpath){
		odb->inpath = char_to_str(inpath, sizeof(inpath)/sizeof(char));
	}
	if(outpath){
		odb->outpath = char_to_str(outpath, sizeof(outpath)/sizeof(char));
	}
	if(runpath){
		odb->runpath = char_to_str(runpath, sizeof(runpath)/sizeof(char));
	}
	if(datasize){
		odb->total_size = ((uint64_t)atoi(datasize))*1024*1024*1024;
	}
	if(memsize){
		odb->mem_size = ((uint64_t)atoi(memsize))*1024*1024;
	}
	if(th){
		odb->nr_runform_th = atoi(th);
		odb->nr_merge_th = atoi(th);
	}
	if(datasize || memsize || th){
		odb->nr_run = (odb->total_size/odb->mem_size) * odb->nr_runform_th;
		assert(odb->nr_run > 0);
		odb->rf_blksize = (odb->mem_size/odb->nr_runform_th);
		assert(odb->rf_blksize >= 4096);
		odb->mrg_blksize = (odb->mem_size/odb->nr_merge_th/odb->nr_run);
		assert(odb->mrg_blksize >= 4096);
	}
}

void
opt_print(struct opt_t *odb)
{
	std::cout << "\n <CONFIGURATONS>" << std::endl;
	std::cout << " TOTAL_DATA_SIZE:     " << odb->total_size/1024/1024/1024 << "GB" << std::endl;
	std::cout << " MEM_SIZE:            " << odb->mem_size/1024/1024 << "MB" << std::endl;
	std::cout << " KV_SIZE(KEY" << odb->key_size << "B):      " << odb->kv_size << "B" << std::endl;
	std::cout << " NRTH_RUNFORM:        " << odb->nr_runform_th << std::endl;
	std::cout << " NRTH_MRG:            " << odb->nr_merge_th << std::endl;
	std::cout << " NR_RUNS:             " << odb->nr_run << std::endl;
	std::cout << " BLKSIZE(RUNFORM):    " << odb->rf_blksize/1024 << "KB" << std::endl;
	std::cout << " BLKSIZE(MERGE):      " << odb->mrg_blksize/1024 << "KB" << std::endl;
	std::cout << " WRITE_UNIT(MERGE):   " << odb->mrg_wrbuf/1024 << "KB" << std::endl;
	std::cout << " TOTAL ENTRIES:       " << odb->total_size/KV_SIZE << std::endl;
}


