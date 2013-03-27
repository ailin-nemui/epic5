#!/bin/bash

# alloc_dump_summary.sh
# summarises the output of the /ALLOCDUMP epic command.
# - caf

last_file_line=""

echo "file/line	alloc_count	alloc_bytes"
cut -f1-2 |
 sort |
 (while read file_line alloc_bytes; do
     if [ "$file_line" != "$last_file_line" ]; then
         if [ "$last_file_line" != "" ]; then
             echo "$last_file_line	$alloc_count	$alloc_total"
         fi
         last_file_line="$file_line"
         alloc_total=$((alloc_bytes))
         alloc_count=1
     else
         alloc_total=$((alloc_total + alloc_bytes))
         alloc_count=$((alloc_count + 1))
	 fi
  done
  if [ "$last_file_line" != "" ]; then
      echo "$last_file_line	$alloc_count	$alloc_total"
  fi)
     
