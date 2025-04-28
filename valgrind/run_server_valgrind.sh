#!/bin/bash

echo "Running server through Valgrind..."
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file="server_valgrind.log" ../chat_app/build/server/server

if [ $? -ne 0 ]; then
    echo "Error: Valgrind encountered issues during execution."
    echo "Check if valgrind is installed by running 'sudo apt-get install valgrind'."
fi

echo "Results saved to server_valgrind.log"
