#**********************************************
# Copyright 2019 Vebjørn Kvisli
# License: GNU Lesser General Public License v3.0
#***********************************************

# Start with base image (fedora 29 with required packages installed)
FROM vkvisli/solarsim_api

# Copy application code to a folder 'app' on the container
COPY . /app

# Compile the simulator
RUN cd app/simulator/DOMINOES/ && make Simulator

# Tell the container to start the javaScript API server on startup
CMD cd app && npm start

# Expose port 8080
EXPOSE 8080

