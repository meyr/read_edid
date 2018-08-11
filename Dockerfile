FROM ubuntu:18.04

RUN apt-get update && apt-get install gcc linux-libc-dev make libi2c-dev -y
CMD ["/bin/bash"]
