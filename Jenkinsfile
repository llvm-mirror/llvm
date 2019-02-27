pipeline {
  agent {
    node {
      label 'ubuntu '
    }

  }
  stages {
    stage('make dir') {
      steps {
        sh '''mkdir evm
'''
      }
    }
    stage('check build') {
      steps {
        sh 'cmake -DLLVM_EXPERIMENTAL_TARGETS_TO_BUILD=evm'
      }
    }
  }
}