#!/bin/bash

# Exit on error
set -e

# Display installation progress
echo "Installing dependencies for Chat Server..."

# Update package lists
echo "Updating package lists..."
sudo apt-get update

# Install build tools
echo "Installing build tools..."
sudo apt-get install -y build-essential cmake pkg-config

# Install GTK3 and development libraries
echo "Installing GTK3 and development libraries..."
sudo apt-get install -y libgtk-3-dev

# Install thread library
echo "Installing pthread library..."
sudo apt-get install -y libpthread-stubs0-dev

# Add any additional dependencies here

echo "All dependencies installed successfully!"
echo "You can now build the Chat Server with 'make' command"

# Make the script executable upon creation
chmod +x "$0" 