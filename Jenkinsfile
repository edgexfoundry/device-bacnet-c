//
// Copyright (c) 2019 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

loadGlobalLibrary()

def image_amd64
def image_arm64

pipeline {
    agent {
        label 'centos7-docker-4c-2g'
    }

    options {
        timestamps()
    }

    stages {
        stage('LF Prep') {
            steps {
                edgeXSetupEnvironment()
            }
        }

        stage('Build Docker Image') {
            parallel {
                stage('amd64') {
                    agent {
                        label 'centos7-docker-4c-2g'
                    }
                    stages {
                        stage('Docker Build') {
                            steps {
                                edgeXDockerLogin(settingsFile: env.MVN_SETTINGS)

                                script {
                                    image_amd64 = docker.build('docker-device-bacnet-c', '-f scripts/Dockerfile.alpine-3.9 .')
                                }
                            }
                        }

                        stage('Docker Push') {
                            when { expression { edgex.isReleaseStream() } }
                            steps {
                                script {
                                    docker.withRegistry("https://${env.DOCKER_REGISTRY}:10004") {
                                        image_amd64.push(env.GIT_COMMIT)
                                        image_amd64.push('1.1.0')
                                    }
                                }
                            }
                        }
                    }
                }

                stage('arm64') {
                    agent {
                        label 'ubuntu18.04-docker-arm64-4c-2g'
                    }
                    stages {
                        stage('Docker Build') {
                            steps {
                                edgeXDockerLogin(settingsFile: env.MVN_SETTINGS)

                                script {
                                    image_arm64 = docker.build('docker-device-bacnet-c-arm64', '-f scripts/Dockerfile.alpine-3.9 .')
                                }
                            }
                        }

                        stage('Docker Push') {
                            when { expression { edgex.isReleaseStream() } }
                            steps {
                                script {
                                    docker.withRegistry("https://${env.DOCKER_REGISTRY}:10004") {
                                        image_arm64.push(env.GIT_COMMIT)
                                        image_arm64.push('1.1.0')
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        stage('Clair Image Scan') {
            when { expression { edgex.isReleaseStream() } }
            steps {
                edgeXClair("${env.DOCKER_REGISTRY}:10004/docker-device-bacnet-c:1.1.0")
                edgeXClair("${env.DOCKER_REGISTRY}:10004/docker-device-bacnet-c-arm64:1.1.0")
            }
        }
    }

    post {
        always {
            edgeXInfraPublish()
        }
    }
}

def loadGlobalLibrary(branch = '*/master') {
    library(identifier: 'edgex-global-pipelines@master', 
        retriever: legacySCM([
            $class: 'GitSCM',
            userRemoteConfigs: [[url: 'https://github.com/edgexfoundry/edgex-global-pipelines.git']],
            branches: [[name: branch]],
            doGenerateSubmoduleConfigurations: false,
            extensions: [[
                $class: 'SubmoduleOption',
                recursiveSubmodules: true,
            ]]]
        )
    ) _
}
