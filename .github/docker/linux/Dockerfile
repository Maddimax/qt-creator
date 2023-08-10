FROM ubuntu:23.04

RUN apt-get update && apt-get install -y p7zip-full libglib2.0 git cmake build-essential curl libdigest-sha-perl sudo && rm -rf /var/lib/apt/lists/*

## Create a folder
RUN mkdir /actions-runner

WORKDIR /actions-runner

## Download the latest runner package
RUN curl -o actions-runner-linux-x64-2.307.1.tar.gz -L https://github.com/actions/runner/releases/download/v2.307.1/actions-runner-linux-x64-2.307.1.tar.gz 
## Optional: Validate the hash
RUN echo "038c9e98b3912c5fd6d0b277f2e4266b2a10accc1ff8ff981b9971a8e76b5441  actions-runner-linux-x64-2.307.1.tar.gz" | shasum -a 256 -c
## Extract the installer
RUN tar xzf ./actions-runner-linux-x64-2.307.1.tar.gz

RUN ./bin/installdependencies.sh

RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

# Drop root privileges
ARG DOCKER_USER=default_user
RUN addgroup --system $DOCKER_USER && adduser --system $DOCKER_USER && adduser $DOCKER_USER sudo
RUN chown -R $DOCKER_USER:$DOCKER_USER /actions-runner
USER $DOCKER_USER

ARG GITHUB_TOKEN
ARG RUNNER_NAME

RUN ./config.sh --url https://github.com/Maddimax/qt-creator --name $RUNNER_NAME --token $GITHUB_TOKEN

CMD ["./run.sh"]

