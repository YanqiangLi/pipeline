###############################################################################
# The MIT License (MIT)
#
# Copyright (c) 2013 Xin Zhong and Charles Lin
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
###############################################################################

# For dependencies, please see https://github.com/BradnerLab/pipeline/wiki/bamliquidator

# I prefer clang++, but g++ is more easily available, so using that instead
#CC=clang++
CC=g++

CPPFLAGS := -std=c++0x -O3 -g -Wall -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -march=native
LDFLAGS := -O3 -g -Wall -march=native
LDLIBS := -lbam -lz -lpthread
ADDITIONAL_LDLIBS := -lhdf5 -lhdf5_hl -ltcmalloc_minimal -ltbb

all: ../bamliquidator bamliquidator_bins bamliquidator_regions 

../bamliquidator: bamliquidator.m.o bamliquidator.o
	$(CC) $(LDFLAGS) -o ../bamliquidator bamliquidator.o bamliquidator.m.o $(LDLIBS) 

bamliquidator_bins: bamliquidator_bins.m.o
	$(CC) $(LDFLAGS) -o bamliquidator_bins bamliquidator.o bamliquidator_bins.m.o \
					$(LDLIBS) $(ADDITIONAL_LDLIBS)

bamliquidator_regions: bamliquidator_regions.m.o bamliquidator.o
	$(CC) $(LDFLAGS) -o bamliquidator_regions bamliquidator.o bamliquidator_regions.m.o \
					$(LDLIBS) $(ADDITIONAL_LDLIBS) 

bamliquidator.m.o: bamliquidator.m.cpp
	$(CC) $(CPPFLAGS) -c bamliquidator.m.cpp

bamliquidator_bins.m.o: bamliquidator_bins.m.cpp
	$(CC) $(CPPFLAGS) -c bamliquidator_bins.m.cpp

bamliquidator_regions.m.o: bamliquidator_regions.m.cpp
	$(CC) $(CPPFLAGS) -c bamliquidator_regions.m.cpp
  
bamliquidator.o: bamliquidator.cpp bamliquidator.h
	$(CC) $(CPPFLAGS) -I$(SAM_DIR) -pthread -c bamliquidator.cpp

clean:
	rm -f ../bamliquidator bamliquidator bamliquidator_bins bamliquidator_regions *.o 