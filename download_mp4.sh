#!/bin/bash

url="$1"
output_file="${2:-output.mp4}"
proxy="socks5://192.168.65.157:10808"
max_retries=100
retry_wait=3

download_with_resume() {
    local retry_count=0
    local start_size=0
    
    # Check if file exists and get its size
    if [ -f "$output_file" ]; then
        start_size=$(stat -f%z "$output_file" 2>/dev/null || stat -c%s "$output_file" 2>/dev/null)
        if [ -z "${start_size}" ] || ! [ "${start_size}" -eq "${start_size}" ] 2>/dev/null; then
            start_size=0
        fi
        echo "Existing file found, size: $start_size bytes"
    else
        echo "Starting new download"
    fi
    
    while [ $retry_count -lt $max_retries ]; do
        echo "Attempting download (attempt $((retry_count + 1)) of $max_retries)..."
        
        # Get total file size first
        total_size=$(curl -sI -x "$proxy" "$url" | grep -i content-length | awk '{print $2}' | tr -d '\r')
        
        if [ -n "${total_size}" ] && [ "${start_size}" -eq "${total_size}" ] 2>/dev/null; then
            echo "File already completely downloaded"
            return 0
        fi

        if [ $start_size -eq 0 ]; then
            # If starting from 0, don't use -C option
            curl -x "$proxy" \
                 -L \
                 --retry 3 \
                 --retry-delay 2 \
                 --connect-timeout 10 \
                 -o "$output_file" \
                 "$url"
        else
            # Only use -C when we have a valid start_size
            curl -x "$proxy" \
                 -C $start_size \
                 -L \
                 --retry 3 \
                 --retry-delay 2 \
                 --connect-timeout 10 \
                 -o "$output_file" \
                 "$url"
        fi

        curl_status=$?
        current_size=$(stat -f%z "$output_file" 2>/dev/null || stat -c%s "$output_file" 2>/dev/null)
        
        # 新的下载完成判断逻辑
        if [ $curl_status -eq 0 ]; then
            if [ -n "${total_size}" ] && [ "${current_size}" -eq "${total_size}" ] 2>/dev/null; then
                echo "Download completed successfully!"
                return 0
            elif [ "${current_size}" -gt 0 ] && [ "${current_size}" -eq "${start_size}" ]; then
                echo "Download seems stuck, current size: ${current_size} bytes"
                sleep $retry_wait
            fi
        elif [ $curl_status -eq 18 ] && [ "${current_size}" -eq "${total_size}" ]; then
            # curl status 18 (transfer error) but we got the full file
            echo "Download completed successfully despite transfer error!"
            return 0
        fi
        
        # Update start_size for next attempt
        if [ "${current_size}" -gt "${start_size}" ]; then
            start_size=$current_size
        fi
        retry_count=$((retry_count + 1))
        
        if [ $retry_count -lt $max_retries ]; then
            echo "Download interrupted at $start_size bytes, waiting $retry_wait seconds before retrying..."
            sleep $retry_wait
        fi
    done
    
    echo "Failed to download after $max_retries attempts"
    return 1
}

if [ -z "$1" ]; then
    echo "Usage: $0 <url> [output_file]"
    exit 1
fi

download_with_resume "$url" "$output_file"
