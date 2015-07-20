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