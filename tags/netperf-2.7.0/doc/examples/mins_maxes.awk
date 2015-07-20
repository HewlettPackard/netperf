BEGIN {
    max_interval = 0.0;
    min_timestamp = 9999999999.0;
    max_timestamp = 0.0;
}

NF == 4 {
    if ($3 > max_interval) max_interval = $3
    if ($4 > max_timestamp) max_timestamp = $4
    if ($4 < min_timestamp) min_timestamp = $4
    next
}

END {
    max_interval = int(max_interval) + 1
    min_timestamp = int(min_timestamp)
    max_timestamp = int(max_timestamp) + 1
    printf("MAX_INTERVAL=%d\nMAX_TIMESTAMP=%d\nMIN_TIMESTAMP=%d\n",
	   max_interval,
	   max_timestamp,
	   min_timestamp)
}