# vTopology: Topology prober for vSched
![vSched](https://img.shields.io/badge/vSched-vTopology-blue)
![License](https://img.shields.io/badge/License-Apache%202.0-green)

**This component is part of the vSched project. The main repository for vSched is located at: https://github.com/vSched/vsched_main**

⚠️ **You must be running the vSched custom kernel with the vSched kernel module activated for this component to function**


## Overview


vTopology is a program used to determine the topology of vCPUs in a virtual machine. It measures cache line transfer latencies between vCPU pairs to infer their relationships, including SMT siblings, vCPUs on the same socket, and stacked vCPUs. 

## Parameters and Configuration

It is reccomended that you use vTop's default options.

### Auto-Configuration

vTopology will automatically scale it's cache-transfer attempts up and down based on it's successes
and failures - more failures will encourage it to try harder, at the cost of performance, and less failures will scale the amount of effort down.

This can be disabled and reenabled with the -o flag


#### Details

| Flag  | Description | Default Value
| ------------- | ------------- | ------------- |
| -v  | Verbosity - Can be a  value between 0-2, inclusive  | 0  |
| -u  | Cache Transfer Attempts  | 10000  |
| -d  | Sample Size  | 150  |
| -g  | Sample Count  | 10 | 
| -f  | Sleep time before probing (seconds)  | 2 | 
| -o  |  Enable Optimizations | True |

