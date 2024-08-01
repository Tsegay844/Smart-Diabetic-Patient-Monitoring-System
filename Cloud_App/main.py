#!/usr/bin/env python3

import subprocess
import signal
import sys

RESET = "\033[0m"
BOLD = "\033[1m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
RED = "\033[31m"

def start_process(script_name):
    return subprocess.Popen(['python3', script_name])

def stop_process(process):
    if process and process.poll() is None:
        process.terminate()
        process.wait()

def stop_all_processes():
    global coap_process, mqtt_process, general_processes

    stop_process(coap_process)
    stop_process(mqtt_process)
    for proc in general_processes:
        stop_process(proc)
    coap_process, mqtt_process, general_processes = None, None, []

def signal_handler(signum, frame):
    print("\nCtrl+C detected! Exiting...")
    cleanup_and_exit()

def cleanup_and_exit():
    stop_all_processes()
    sys.exit(0)

coap_process, mqtt_process, general_processes = None, None, []

def main():
    global coap_process, mqtt_process, general_processes

    while True:
        print(f"{BOLD}{GREEN}Select an option:{RESET}")
        print(f"{YELLOW}1> Monitor Diabetics{RESET}")
        print(f"{YELLOW}2> Monitor Cardiovascular{RESET}")
        print(f"{YELLOW}3> General Monitoring{RESET}")
        print(f"{RED}0> Exit{RESET}")

        choice = input("Enter your choice: ")

        if choice == '1':
            stop_all_processes()
            coap_process = start_process('coap.py')

            print("Do you want to:")
            print("1> Return to main menu")
            print("0> Exit")
            sub_choice = input("Enter your choice: ")
            if sub_choice == '1':
                stop_all_processes()
                continue
            elif sub_choice == '0':
                print("{RED}Exiting.....{RESET}")
                stop_all_processes()
                break

        elif choice == '2':
            stop_all_processes()
            mqtt_process = start_process('mqtt.py')

            print("Do you want to:")
            print("1> Return to main menu")
            print("0> Exit")
            sub_choice = input("Enter your choice: ")
            if sub_choice == '1':
                stop_all_processes()
                continue
            elif sub_choice == '0':
                print("Exiting...")
                stop_all_processes()
                break

        elif choice == '3':
            stop_all_processes()
            coap_process = start_process('coap.py')
            mqtt_process = start_process('mqtt.py')
            keypad_process = start_process('keypad.py')
            general_processes = [coap_process, mqtt_process, keypad_process]

        elif choice == '0':
            print("Exiting...")
            stop_all_processes()
            break

        else:
            print("Invalid choice. Please enter 1, 2, 3, or 0.")

if __name__ == '__main__':
    signal.signal(signal.SIGINT, signal_handler)
    main()
