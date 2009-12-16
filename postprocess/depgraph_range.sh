#!/bin/sh

while (( "$#" )); do
  case $1 in
    --min) shift
         min=" min=$1";;
    --max) shift
         max=" max=$1";;
    --src-file) shift
         sourcefile=$1;;
    --dep-file) shift
         depfile=$1;;
    --sraw) sraw=" sraw=1";;
    --swar) swar=" swar=1";;
    --swaw) swaw=" swaw=1";;
    --sctl) sctl=" sctl=1";;
    --track-hidden) track_hidden=" track_hidden=1";;
    --track-stack-name-deps) track_stack_name_deps=" track_stack_name_deps=1";;
    *) echo "Unknown option."
       exit 1;;
  esac
  shift
done

exec gawk -f /home/jchm2/Work/embla/postprocess/depgraph_range.awk $min$max sourcefile=$sourcefile $sraw$swar$swaw$sctl$track_hidden$track_stack_name_deps file=source $sourcefile file=deps $depfile

