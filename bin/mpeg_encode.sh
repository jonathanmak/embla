#! /bin/sh

set -ex

postproc=$(pwd)
embla_root=${postproc}/..

valgrind=$1

if [ ! -x "$valgrind" ] ; then
  echo "Usage: $0 path_to_valgrind"
  exit 1
fi

if [ ! -f depgraph.awk ]; then
  echo "Run this script from the postprocess directory"
  exit 1
fi

tmp=/tmp/${LOGNAME}/embla_mpeg_test
mkdir -p $tmp
cd $tmp

decoder_dir=${embla_root}/contrib/applications/mpeg2_video_ref/decoder
make -C ${decoder_dir}

encoder_dir=${embla_root}/contrib/applications/mpeg2_video_ref/encoder
make -C ${encoder_dir}

data_dir=${embla_root}/contrib/data
source_mpeg=${data_dir}/bike.mpg

if [ ! -f bike_frame1210.U ] ; then
  ${decoder_dir}/mpeg2decode -b ${source_mpeg} -o0 'bike_frame%d'
fi

$valgrind --tool=embla \
 ${encoder_dir}/mpeg2encode ${data_dir}/bike.par bike_out.mpg
