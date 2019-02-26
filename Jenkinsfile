pipeline {
  agent any
  stages {
    stage('make dir') {
      steps {
        sh '''mkdir /opt/evm
'''
      }
    }
    stage('check build') {
      steps {
        sh 'cmake -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=/opt/evm'
      }
    }
  }
}