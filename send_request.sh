#!/bin/bash

: "
+-----------------------------------------------------------------------------------+
|   This script sends (n) HTTP requests simultaneously, it is used for testing the  |
|   web server's performance.                                                       |
+-----------------------------------------------------------------------------------+
                                                                \   ^__^
                                                                 \  (oo)\_______
                                                                    (__)\       )\/\
                                                                        ||----w |
                                                                        ||     ||                           
"

if [ $# -ne 2 ]; then
    echo "Usage: $0 <url> <num_requests>"
    exit 1
fi

URL="$1"
NUM_REQUESTS="$2"

send_request() {
    curl --tlsv1.2 -s -o /dev/null "$URL" > /dev/null 2>&1
}

# Send requests simultaneously
for ((i=0; i<NUM_REQUESTS; i++)); do
    send_request &
done

# Wait for all background jobs to finish
wait
