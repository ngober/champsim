#!/usr/bin/env python3
import json
import sys,os
import itertools
import functools
import copy
from collections import ChainMap

constants_header_name = 'inc/champsim_constants.h'
instantiation_file_name = 'src/core_inst.cc'
config_cache_name = '.champsimconfig_cache'

###
# Begin format strings
###

dcn_fmtstr = 'cpu{}_{}' # default cache name format string

cache_fmtstr = 'CACHE {name}("{name}", {frequency}, {sets}, {ways}, {wq_size}, {rq_size}, {pq_size}, {mshr_size}, {hit_latency}, {fill_latency}, {max_read}, {max_write}, {prefetch_as_load:b}, {lower_level}, &CACHE::{replacement_initialize}, &CACHE::{replacement_find_victim}, &CACHE::{replacement_update_replacement_state}, &CACHE::{replacement_replacement_final_stats});\n'

cpu_fmtstr = 'O3_CPU cpu{cpu}_inst({cpu}, {attrs[frequency]}, {attrs[DIB][sets]}, {attrs[DIB][ways]}, {attrs[DIB][window_size]}, {attrs[ifetch_buffer_size]}, {attrs[dispatch_buffer_size]}, {attrs[decode_buffer_size]}, {attrs[rob_size]}, {attrs[lq_size]}, {attrs[sq_size]}, {attrs[fetch_width]}, {attrs[decode_width]}, {attrs[dispatch_width]}, {attrs[scheduler_size]}, {attrs[execute_width]}, {attrs[lq_width]}, {attrs[sq_width]}, {attrs[retire_width]}, {attrs[mispredict_penalty]}, {attrs[decode_latency]}, {attrs[dispatch_latency]}, {attrs[schedule_latency]}, {attrs[execute_latency]}, &{attrs[ITLB]}, &{attrs[DTLB]}, &{attrs[L1I]}, &{attrs[L1D]});\n'

pmem_fmtstr = 'MEMORY_CONTROLLER DRAM("DRAM", {attrs[frequency]});\n'
vmem_fmtstr = 'VirtualMemory vmem(NUM_CPUS, {attrs[size]}, PAGE_SIZE, {attrs[num_levels]}, 1);\n'

module_make_fmtstr = '{1}/%.o: CFLAGS += -I{1}\n{1}/%.o: CXXFLAGS += -I{1}\n{1}/%.o: CXXFLAGS += {2}\nobj/{0}: $(patsubst %.cc,%.o,$(wildcard {1}/*.cc)) $(patsubst %.c,%.o,$(wildcard {1}/*.c))\n\t@mkdir -p $(dir $@)\n\tar -rcs $@ $^\n\n'

define_fmtstr = '#define {{names[{name}]}} {{config[{name}]}}u\n'
define_nonint_fmtstr = '#define {{names[{name}]}} {{config[{name}]}}\n'
define_log_fmtstr = '#define LOG2_{{names[{name}]}} lg2({{names[{name}]}})\n'

###
# Begin named constants
###

const_names = {
    'block_size': 'BLOCK_SIZE',
    'page_size': 'PAGE_SIZE',
    'heartbeat_frequency': 'STAT_PRINTING_PERIOD',
    'num_cores': 'NUM_CPUS',
    'physical_memory': {
        'io_freq': 'DRAM_IO_FREQ',
        'channels': 'DRAM_CHANNELS',
        'ranks': 'DRAM_RANKS',
        'banks': 'DRAM_BANKS',
        'rows': 'DRAM_ROWS',
        'columns': 'DRAM_COLUMNS',
        'row_size': 'DRAM_ROW_SIZE',
        'channel_width': 'DRAM_CHANNEL_WIDTH',
        'wq_size': 'DRAM_WQ_SIZE',
        'rq_size': 'DRAM_RQ_SIZE',
        'tRP': 'tRP_DRAM_NANOSECONDS',
        'tRCD': 'tRCD_DRAM_NANOSECONDS',
        'tCAS': 'tCAS_DRAM_NANOSECONDS'
    }
}

###
# Begin default core model definition
###

default_root = { 'executable_name': 'bin/champsim', 'block_size': 64, 'page_size': 4096, 'heartbeat_frequency': 10000000, 'num_cores': 1, 'DIB': {}, 'L1I': {}, 'L1D': {}, 'L2C': {}, 'ITLB': {}, 'DTLB': {}, 'STLB': {}, 'LLC': {}, 'physical_memory': {}, 'virtual_memory': {}}

# Read the config file
if len(sys.argv) >= 2:
    with open(sys.argv[1]) as rfp:
        config_file = ChainMap(json.load(rfp), default_root)
else:
    print("No configuration specified. Building default ChampSim with no prefetching.")
    config_file = ChainMap(default_root)

default_core = { 'frequency' : 4000, 'ifetch_buffer_size': 64, 'decode_buffer_size': 32, 'dispatch_buffer_size': 32, 'rob_size': 352, 'lq_size': 128, 'sq_size': 72, 'fetch_width' : 6, 'decode_width' : 6, 'dispatch_width' : 6, 'execute_width' : 4, 'lq_width' : 2, 'sq_width' : 2, 'retire_width' : 5, 'mispredict_penalty' : 1, 'scheduler_size' : 128, 'decode_latency' : 1, 'dispatch_latency' : 1, 'schedule_latency' : 0, 'execute_latency' : 0, 'branch_predictor': 'bimodal', 'btb': 'basic_btb' }
default_dib  = { 'window_size': 16,'sets': 32, 'ways': 8 }
default_l1i  = { 'sets': 64, 'ways': 8, 'rq_size': 64, 'wq_size': 64, 'pq_size': 32, 'mshr_size': 8, 'latency': 4, 'fill_latency': 1, 'max_read': 2, 'max_write': 2, 'prefetch_as_load': False, 'prefetcher': 'no_l1i', 'replacement': 'lru'}
default_l1d  = { 'sets': 64, 'ways': 12, 'rq_size': 64, 'wq_size': 64, 'pq_size': 8, 'mshr_size': 16, 'latency': 5, 'fill_latency': 1, 'max_read': 2, 'max_write': 2, 'prefetch_as_load': False, 'prefetcher': 'no_l1d', 'replacement': 'lru'}
default_l2c  = { 'sets': 1024, 'ways': 8, 'rq_size': 32, 'wq_size': 32, 'pq_size': 16, 'mshr_size': 32, 'latency': 10, 'fill_latency': 1, 'max_read': 1, 'max_write': 1, 'prefetch_as_load': False, 'prefetcher': 'no_l2c', 'replacement': 'lru'}
default_itlb = { 'sets': 16, 'ways': 4, 'rq_size': 16, 'wq_size': 16, 'pq_size': 0, 'mshr_size': 8, 'latency': 1, 'fill_latency': 1, 'max_read': 2, 'max_write': 2, 'prefetch_as_load': False, 'replacement': 'lru'}
default_dtlb = { 'sets': 16, 'ways': 4, 'rq_size': 16, 'wq_size': 16, 'pq_size': 0, 'mshr_size': 8, 'latency': 1, 'fill_latency': 1, 'max_read': 2, 'max_write': 2, 'prefetch_as_load': False, 'replacement': 'lru'}
default_stlb = { 'sets': 128, 'ways': 12, 'rq_size': 32, 'wq_size': 32, 'pq_size': 0, 'mshr_size': 16, 'latency': 8, 'fill_latency': 1, 'max_read': 1, 'max_write': 1, 'prefetch_as_load': False, 'replacement': 'lru'}
default_llc  = { 'sets': 2048*config_file['num_cores'], 'ways': 16, 'rq_size': 32*config_file['num_cores'], 'wq_size': 32*config_file['num_cores'], 'pq_size': 32*config_file['num_cores'], 'mshr_size': 64*config_file['num_cores'], 'latency': 20, 'fill_latency': 1, 'max_read': config_file['num_cores'], 'max_write': config_file['num_cores'], 'prefetch_as_load': False, 'prefetcher': 'no_llc', 'replacement': 'lru', 'name': 'LLC', 'lower_level': 'DRAM' }
default_pmem = { 'frequency': 3200, 'channels': 1, 'ranks': 1, 'banks': 8, 'rows': 65536, 'columns': 128, 'row_size': 8, 'channel_width': 8, 'wq_size': 64, 'rq_size': 64, 'tRP': 12.5, 'tRCD': 12.5, 'tCAS': 12.5 }
default_vmem = { 'size': 8589934592, 'num_levels': 5 }

###
# Ensure directories are present
###

os.makedirs(os.path.dirname(config_file['executable_name']), exist_ok=True)
os.makedirs(os.path.dirname(instantiation_file_name), exist_ok=True)
os.makedirs(os.path.dirname(constants_header_name), exist_ok=True)
os.makedirs('obj', exist_ok=True)

###
# Establish default optional values
###

config_file['physical_memory'] = ChainMap(config_file['physical_memory'], default_pmem.copy())
config_file['virtual_memory'] = ChainMap(config_file['virtual_memory'], default_vmem.copy())

cores = config_file.get('ooo_cpu', [{}])

# Index the cache array by names
caches = {c['name']: c for c in config_file.get('cache',[])}

# Default branch predictor and BTB
for i in range(len(cores)):
    cores[i] = ChainMap(cores[i], copy.deepcopy(default_root), default_core.copy())
    cores[i]['DIB'] = ChainMap(cores[i]['DIB'], config_file['DIB'].copy(), default_dib.copy())

# Copy or trim cores as necessary to fill out the specified number of cores
original_size = len(cores)
if original_size <= config_file['num_cores']:
    for i in range(original_size, config_file['num_cores']):
        cores.append(copy.deepcopy(cores[(i-1) % original_size]))
else:
    cores = config_file[:(config_file['num_cores'] - original_size)]

# Append LLC to cache array
# LLC operates at maximum freqency of cores, if not already specified
caches['LLC'] = ChainMap(caches.get('LLC',{}), config_file['LLC'].copy(), {'frequency': max(cpu['frequency'] for cpu in cores)}, default_llc.copy())

# If specified in the core, move definition to cache array
for i, cpu in enumerate(cores):
    for cache_name in ('L1I', 'L1D', 'L2C', 'ITLB', 'DTLB', 'STLB'):
        if isinstance(cpu[cache_name], dict):
            cpu[cache_name] = ChainMap(cpu[cache_name], {'name': dcn_fmtstr.format(i,cache_name)})
            caches[cpu[cache_name]['name']] = cpu[cache_name]
            cpu[cache_name] = cpu[cache_name]['name']

# Assign defaults that are unique per core
for cpu in cores:
    caches[cpu['L1I']] = ChainMap(caches[cpu['L1I']], {'frequency': cpu['frequency'], 'lower_level': cpu['L2C']}, default_l1i.copy())
    caches[cpu['L1D']] = ChainMap(caches[cpu['L1D']], {'frequency': cpu['frequency'], 'lower_level': cpu['L2C']}, default_l1d.copy())
    caches[cpu['ITLB']] = ChainMap(caches[cpu['ITLB']], {'frequency': cpu['frequency'], 'lower_level': cpu['STLB']}, default_itlb.copy())
    caches[cpu['DTLB']] = ChainMap(caches[cpu['DTLB']], {'frequency': cpu['frequency'], 'lower_level': cpu['STLB']}, default_dtlb.copy())

    # L2C
    cache_name = caches[cpu['L1D']]['lower_level']
    if cache_name != 'DRAM':
        caches[cache_name] = ChainMap(caches[cache_name], {'frequency': cpu['frequency'], 'lower_level': 'LLC'}, default_l2c.copy())

    # STLB
    cache_name = caches[cpu['DTLB']]['lower_level']
    if cache_name != 'DRAM':
        caches[cache_name] = ChainMap(caches[cache_name], {'frequency': cpu['frequency']}, default_l2c.copy())

# Establish default lower levels if they do not exist
for cache in caches.values():
    cache.setdefault('lower_level', None)

# Remove caches that are inaccessible
accessible = [False]*len(caches)
for i,ll in enumerate(caches.values()):
    accessible[i] |= any(ul['lower_level'] == ll['name'] for ul in caches.values()) # The cache is accessible from another cache
    accessible[i] |= any(ll['name'] in [cpu['L1I'], cpu['L1D'], cpu['ITLB'], cpu['DTLB']] for cpu in cores) # The cache is accessible from a core
caches = dict(itertools.compress(caches.items(), accessible))

# Establish latencies in caches
for cache in caches.values():
    cache['hit_latency'] = cache.get('hit_latency') or (cache['latency'] - cache['fill_latency'])

# Scale frequencies
config_file['physical_memory']['io_freq'] = config_file['physical_memory']['frequency'] # Save value
freqs = list(itertools.chain(
    [cpu['frequency'] for cpu in cores],
    [cache['frequency'] for cache in caches.values()],
    (config_file['physical_memory']['frequency'],)
))
freqs = [max(freqs)/x for x in freqs]
for freq,src in zip(freqs, itertools.chain(cores, caches.values(), (config_file['physical_memory'],))):
    src['frequency'] = freq

###
# Check to make sure modules exist and they correspond to any already-built modules.
###

# derive function names for replacement
for cache in caches.values():
    if cache['replacement'] is not None:
        cache['replacement_initialize'] = 'repl_' + os.path.basename(cache['replacement']) + '_initialize'
        cache['replacement_find_victim'] = 'repl_' + os.path.basename(cache['replacement']) + '_victim'
        cache['replacement_update_replacement_state'] = 'repl_' + os.path.basename(cache['replacement']) + '_update'
        cache['replacement_replacement_final_stats'] = 'repl_' + os.path.basename(cache['replacement']) + '_final_stats'

# Associate modules with paths
libfilenames = {}
for i,cpu in enumerate(cores[:1]):
    if caches[cpu['L1I']]['prefetcher'] is not None:
        libfilenames['cpu' + str(i) + 'l1iprefetcher.a'] = ('prefetcher/' + caches[cpu['L1I']]['prefetcher'], '')
    if caches[cpu['L1D']]['prefetcher'] is not None:
        libfilenames['cpu' + str(i) + 'l1dprefetcher.a'] = ('prefetcher/' + caches[cpu['L1D']]['prefetcher'], '')
    if caches[caches[cpu['L1D']]['lower_level']]['prefetcher'] is not None:
        libfilenames['cpu' + str(i) + 'l2cprefetcher.a'] = ('prefetcher/' + caches[caches[cpu['L1D']]['lower_level']]['prefetcher'], '')
    if cpu['branch_predictor'] is not None:
        libfilenames['cpu' + str(i) + 'branch_predictor.a'] = ('branch/' + cpu['branch_predictor'], '')
    if cpu['btb'] is not None:
        libfilenames['cpu' + str(i) + 'btb.a'] = ('btb/' + cpu['btb'], '')
if caches['LLC']['prefetcher'] is not None:
    libfilenames['llprefetcher.a'] = ('prefetcher/' + caches['LLC']['prefetcher'], '')

for cache in caches.values():
    if cache['replacement'] is not None:
        fname = 'replacement/' + cache['replacement']
        libfilenames['repl_' + cache['replacement'] + '.a'] = (fname, '-Dinitialize_replacement=repl_$(notdir {0})_initialize -Dfind_victim=repl_$(notdir {0})_victim -Dupdate_replacement_state=repl_$(notdir {0})_update -Dreplacement_final_stats=repl_$(notdir {0})_final_stats'.format(fname))

# Assert module paths exist
for path,_ in libfilenames.values():
    if not os.path.exists(path):
        print('Path "' + path + '" does not exist. Exiting...')
        sys.exit(1)

# Check cache of previous configuration
if os.path.exists(config_cache_name):
    with open(config_cache_name) as rfp:
        config_cache = json.load(rfp)
else:
    config_cache = {k:'' for k in libfilenames}

# Prune modules whose configurations have changed (force make to rebuild it)
for f in os.listdir('obj'):
    if f in libfilenames and f in config_cache and config_cache[f] != libfilenames[f]:
        os.remove('obj/' + f)

###
# Perform final preparations for file writing
###

# Remove name index
caches = list(caches.values())

# Sort so that lower levels are forward-declared
def level_cmp(cache_a, cache_b):
    if cache_a['lower_level'] == cache_b['name']:
        return -1
    return 1

caches.sort(key=functools.cmp_to_key(level_cmp), reverse=True)

# Check for lower levels in the array
for i in reversed(range(len(caches))):
    ul = caches[i]
    if ul['lower_level'] != 'DRAM' and ul['lower_level'] is not None:
        if not any((ul['lower_level'] == ll['name']) for ll in caches[:i]):
            print('Could not find cache "' + ul['lower_level'] + '" in cache array. Exiting...')
            sys.exit(1)

# prune Nones
for cache in caches:
    if cache['lower_level'] is not None:
        cache['lower_level'] = '&'+cache['lower_level'] # append address operator for C++
    else:
        cache['lower_level'] = 'NULL'

###
# Begin file writing
###

# Instantiation file
with open(instantiation_file_name, 'wt') as wfp:
    wfp.write('/***\n * THIS FILE IS AUTOMATICALLY GENERATED\n * Do not edit this file. It will be overwritten when the configure script is run.\n ***/\n\n')
    wfp.write('#include "cache.h"\n')
    wfp.write('#include "champsim.h"\n')
    wfp.write('#include "dram_controller.h"\n')
    wfp.write('#include "ooo_cpu.h"\n')
    wfp.write('#include "vmem.h"\n')
    wfp.write('#include "operable.h"\n')
    wfp.write('#include "' + os.path.basename(constants_header_name) + '"\n')
    wfp.write('#include <array>\n')
    wfp.write('#include <vector>\n')

    wfp.write(pmem_fmtstr.format(attrs=config_file['physical_memory']))
    for cache in caches:
        wfp.write(cache_fmtstr.format(**cache))

    for i,cpu in enumerate(cores):
        wfp.write(cpu_fmtstr.format(cpu=i, attrs=cpu))

    wfp.write('std::array<O3_CPU*, NUM_CPUS> ooo_cpu {\n')
    for i in range(len(cores)):
        if i > 0:
            wfp.write(',\n')
        wfp.write('&cpu{}_inst'.format(i))
    wfp.write('\n};\n')

    wfp.write('std::array<CACHE*, NUM_CACHES> caches {\n')
    for i,cache in enumerate(caches):
        if i > 0:
            wfp.write(',')
        wfp.write('&{name}'.format(**cache))
    wfp.write('\n};\n')

    wfp.write('std::array<champsim::operable*, NUM_OPERABLES> operables {\n')
    for i in range(len(cores)):
        wfp.write('&cpu{}_inst, '.format(i))
    wfp.write('\n')

    for cache in reversed(caches):
        wfp.write('&{name}, '.format(**cache))

    wfp.write('\n&DRAM')
    wfp.write('\n};\n')

    wfp.write(vmem_fmtstr.format(attrs=config_file['virtual_memory']))
    wfp.write('\n')

# Cache modules file
repl_inits   = {c['replacement_initialize'] for c in caches}
repl_victims = {c['replacement_find_victim'] for c in caches}
repl_updates = {c['replacement_update_replacement_state'] for c in caches}
repl_finals  = {c['replacement_replacement_final_stats'] for c in caches}
with open('inc/cache_modules.inc', 'wt') as wfp:
    for i in repl_inits:
        wfp.write('void ' + i + '();\n')

    for v in repl_victims:
        wfp.write('uint32_t ' + v + '(uint32_t, uint64_t, uint32_t, const BLOCK*, uint64_t, uint64_t, uint32_t);\n')

    for u in repl_updates:
        wfp.write('void ' + u + '(uint32_t, uint32_t, uint32_t, uint64_t, uint64_t, uint64_t, uint32_t, uint8_t);\n')

    for f in repl_finals:
        wfp.write('void ' + f + '();\n')

# Constants header
with open(constants_header_name, 'wt') as wfp:
    wfp.write('/***\n * THIS FILE IS AUTOMATICALLY GENERATED\n * Do not edit this file. It will be overwritten when the configure script is run.\n ***/\n\n')
    wfp.write('#ifndef CHAMPSIM_CONSTANTS_H\n')
    wfp.write('#define CHAMPSIM_CONSTANTS_H\n')
    wfp.write('#include "util.h"\n')
    wfp.write(define_fmtstr.format(name='block_size').format(names=const_names, config=config_file))
    wfp.write(define_log_fmtstr.format(name='block_size').format(names=const_names, config=config_file))
    wfp.write(define_fmtstr.format(name='page_size').format(names=const_names, config=config_file))
    wfp.write(define_log_fmtstr.format(name='page_size').format(names=const_names, config=config_file))
    wfp.write(define_fmtstr.format(name='heartbeat_frequency').format(names=const_names, config=config_file))
    wfp.write(define_fmtstr.format(name='num_cores').format(names=const_names, config=config_file))
    wfp.write('#define NUM_CACHES ' + str(len(caches)) + 'u\n')
    wfp.write('#define NUM_OPERABLES ' + str(len(cores) + len(caches) + 1) + 'u\n')

    for k in const_names['physical_memory']:
        if k in ['tRP', 'tRCD', 'tCAS']:
            wfp.write(define_nonint_fmtstr.format(name=k).format(names=const_names['physical_memory'], config=config_file['physical_memory']))
        else:
            wfp.write(define_fmtstr.format(name=k).format(names=const_names['physical_memory'], config=config_file['physical_memory']))
        if k in ['channels', 'ranks', 'banks', 'rows', 'columns']:
            wfp.write(define_log_fmtstr.format(name=k).format(names=const_names['physical_memory'], config=config_file['physical_memory']))

    wfp.write('#endif\n')

# Makefile
with open('Makefile', 'wt') as wfp:
    wfp.write('CC := ' + config_file.get('CC', 'gcc') + '\n')
    wfp.write('CXX := ' + config_file.get('CXX', 'g++') + '\n')
    wfp.write('CFLAGS := ' + config_file.get('CFLAGS', '-Wall -O3') + ' -std=gnu99\n')
    wfp.write('CXXFLAGS := ' + config_file.get('CXXFLAGS', '-Wall -O3') + ' -std=c++11\n')
    wfp.write('CPPFLAGS := ' + config_file.get('CPPFLAGS', '') + ' -Iinc -MMD -MP\n')
    wfp.write('LDFLAGS := ' + config_file.get('LDFLAGS', '') + '\n')
    wfp.write('LDLIBS := ' + config_file.get('LDLIBS', '') + '\n')
    wfp.write('\n')
    wfp.write('.phony: all clean\n\n')
    wfp.write('all: ' + config_file['executable_name'] + '\n\n')
    wfp.write('clean: \n\t find . -name \*.o -delete\n\t find . -name \*.d -delete\n\t $(RM) -r obj\n\n')
    wfp.write(config_file['executable_name'] + ': $(patsubst %.cc,%.o,$(wildcard src/*.cc)) ' + ' '.join('obj/' + k for k in libfilenames) + '\n')
    wfp.write('\t$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)\n\n')

    for k,v in libfilenames.items():
        wfp.write(module_make_fmtstr.format(k, *v))

    wfp.write('-include $(wildcard prefetcher/*/*.d)\n')
    wfp.write('-include $(wildcard branch/*/*.d)\n')
    wfp.write('-include $(wildcard btb/*/*.d)\n')
    wfp.write('-include $(wildcard replacement/*/*.d)\n')
    wfp.write('-include $(wildcard src/*.d)\n')
    wfp.write('\n')

# Configuration cache
with open(config_cache_name, 'wt') as wfp:
    json.dump(libfilenames, wfp)

