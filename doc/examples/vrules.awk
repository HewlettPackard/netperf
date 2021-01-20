#  Copyright 2021 Hewlett Packard Enterprise Development LP
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
#
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
# DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
# OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
# USE OR OTHER DEALINGS IN THE SOFTWARE.

BEGIN {
    count=0
    resumes=0
    my_index=0
}

    $1 == "Starting" &&
    $2 == "netperfs" &&
    $3 == "on" {
	count += 1
	next
    }

    $1 == "Pausing" {
	printf("VRULE_TIME[%d]=%d\n",my_index,int($6))
	printf("VRULE_COLR[%d]=\"FF0000\"\n",my_index)
	printf("VRULE_TEXT[%d]=\"%d_netperfs_running\"\n",my_index,$8)
	my_index += 1
	next
    }

    $1 == "Resuming" {
	printf("VRULE_TIME[%d]=%d\n",my_index,int($3))
	printf("VRULE_COLR[%d]=\"000000\"\n",my_index)
	if (resumes)
	    printf("VRULE_TEXT[%d]=\"\"\n",my_index)
	else
	    printf("VRULE_TEXT[%d]=\"Resuming_ramp\"\n",my_index)
	resumes=1
	my_index += 1
	next
    }

    $1 == "Netperfs" &&
    $2 == "started" {
	printf("VRULE_TIME[%d]=%d\n",my_index,int($4))
	printf("VRULE_COLR[%d]=\"FF0000\"\n",my_index)
	printf("VRULE_TEXT[%d]=\"All_%d_netperfs_started\"\n",my_index,count)
	my_index += 1
	next
    }

    $1 == "Netperfs" &&
    $2 == "stopping" {
	printf("VRULE_TIME[%d]=%d\n",my_index,int($3))
	printf("VRULE_COLR[%d]=\"000000\"\n",my_index)
	printf("VRULE_TEXT[%d]=\"Rampdown_started\"\n",my_index)
	my_index += 1;
	next
    }

END {
	printf("NUM_VRULES=%d\n",my_index)
    }
