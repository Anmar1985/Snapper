#!/usr/bin/python3
import ctypes
import api
import os
import time
import sys
import struct

def Main():
        command = 1
        #api.ServoShutdown()
        try:
                if api.Initialize():
                        print("Initalized")
                        command = 1
                else:
                        print("Intialization failed")
                #api.ServoShutdown()
                api.PlayAction(8)
                print('Stand up')
                #value = int(input("Turn head to"))
                #api.SetMotorValue(20, value)
                Run(command)
        except (KeyboardInterrupt):
                api.ServoShutdown()
                sys.exit()
        except():
                api.ServoShutdown()
                sys.exit()

def Run(command):
        #api.Walk(True)
        #print("Running...")
        #move foward
        if(command == 1):
          api.PlayAction(15)
          print('Sit')
          command = 0
                #api.WalkMove(0)
                #api.Walk(True)
                #api.WalkMove(20)
        #move back
        elif(command == 0):
                api.PlayAction(8)
                print('Stand up')
                command = 1
                #print("supposed to move backwards")
        #move right
        elif(command == 8):
                api.WalkMove(0)
                api.WalkTurn(20)
        #move left
        elif(command == 9):
                api.WalkMove(0)
                api.WalkTurn(-20)
        elif(command == 5):
                api.WalkMove(0)
        Run(command)


if __name__ == "__main__": 
  Main()
