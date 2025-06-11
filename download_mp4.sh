#!/bin/bash

url="$1"
output_file="${2:-output.mp4}"
proxy="socks5://192.168.46.198:10808"
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
        
        # Improved total size detection with better parsing
        total_size=$(curl -sI -x "$proxy" "$url" | grep -i '^content-length:' | awk '{print $2}' | tr -d '\r\n' | grep -o '^[0-9]*')
        if [ -n "$total_size" ]; then
            echo "Debug: Detected total size: $total_size bytes"
        else
            echo "Warning: Could not determine total file size"
        fi
        
        # Validate sizes before comparison
        if [ -n "$total_size" ] && [ "$total_size" -eq "$total_size" ] 2>/dev/null && \
           [ -n "$start_size" ] && [ "$start_size" -eq "$start_size" ] 2>/dev/null && \
           [ "$start_size" -eq "$total_size" ]; then
            echo "File already completely downloaded"
            return 0
        fi

        # Download attempt with progress output
        if [ ${start_size:-0} -eq 0 ]; then
            echo "Starting fresh download..."
            curl -x "$proxy" \
                 -L \
                 --retry 3 \
                 --retry-delay 2 \
                 --connect-timeout 10 \
                 -o "$output_file" \
                 -#  \
                 "$url"
        else
            echo "Resuming from byte position $start_size..."
            curl -x "$proxy" \
                 -C $start_size \
                 -L \
                 --retry 3 \
                 --retry-delay 2 \
                 --connect-timeout 10 \
                 -o "$output_file" \
                 -#  \
                 "$url"
        fi

        curl_status=$?
        echo "Debug: curl exit status: $curl_status"
        
        # Get current file size with better error handling
        current_size=$(stat -f%z "$output_file" 2>/dev/null || stat -c%s "$output_file" 2>/dev/null)
        echo "Debug: Current file size: ${current_size:-0} bytes"
        
        # Improved completion check with better validation
        if [ $curl_status -eq 0 ]; then
            if [ -n "$total_size" ] && [ "$total_size" -eq "$total_size" ] 2>/dev/null && \
               [ -n "$current_size" ] && [ "$current_size" -eq "$current_size" ] 2>/dev/null && \
               [ "$current_size" -eq "$total_size" ]; then
                echo "Download completed successfully!"
                return 0
            elif [ -n "$current_size" ] && [ -n "$start_size" ] && \
                 [ "$current_size" -eq "$current_size" ] 2>/dev/null && \
                 [ "$start_size" -eq "$start_size" ] 2>/dev/null && \
                 [ "$current_size" -eq "$start_size" ]; then
                echo "Download seems stuck (no new data received)"
                echo "Current size: $current_size bytes"
                echo "Previous size: $start_size bytes"
            fi
        elif [ $curl_status -eq 18 ] && [ -n "${total_size}" ] && [ ${current_size:-0} -eq ${total_size} ]; then
            echo "Download completed successfully despite transfer error!"
            return 0
        else
            echo "Download failed with curl status $curl_status"
        fi
        
        # Update start_size for next attempt
        if [ ${current_size:-0} -gt ${start_size:-0} ]; then
            start_size=$current_size
        fi
        
        retry_count=$((retry_count + 1))
        
        if [ $retry_count -lt $max_retries ]; then
            echo "Download interrupted at ${start_size:-0} bytes, waiting $retry_wait seconds before retrying..."
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
