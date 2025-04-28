#!/bin/bash

echo "Running client through Valgrind..."
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file="client_valgrind.log" ../chat_app/build/client/client

if [ $? -ne 0 ]; then
    echo "Error: Valgrind encountered issues during execution."
    echo "Check if valgrind is installed by running 'sudo apt-get install valgrind'."
fi

echo "Results saved to client_valgrind.log"
