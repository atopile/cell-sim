import argparse
from lib.client import CellSimClient


def main():
    parser = argparse.ArgumentParser(description="Cell Simulator Client CLI")
    parser.add_argument("-p", "--port", type=str, required=True, help="Serial port to connect to")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baud rate, default is 115200")
    args = parser.parse_args()

    try:
        client = CellSimClient(args.port, baudrate=args.baudrate)
    except Exception as e:
        print(f"Error initializing CellSimClient: {e}")
        return

    print(f"Connected to {args.port}")
    print("Enter commands (e.g., SETV 1 3.7, GETV 1, ENABLE_OUTPUT 1, ENABLE_DMM 1, DISABLE_DMM):")
    
    try:
        while True:
            cmd = input(">> ").strip()
            if cmd.lower() in ("exit", "quit"):
                break
            if not cmd:
                continue
            response = client.send_command(cmd)
            for line in response:
                print(line)
    except KeyboardInterrupt:
        pass
    finally:
        client.close()
        print("Serial connection closed.")


if __name__ == '__main__':
    main() 