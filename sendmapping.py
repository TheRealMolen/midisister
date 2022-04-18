import os.path, re, sys
import serial           # pip install pyserial

MAPPINGDIR = os.path.join(os.path.dirname(__file__), 'mappings')

def get_mapping(mappingname):
    if '.txt' not in mappingname:
        mappingname += '.txt'
    
    mappingfile = os.path.join(MAPPINGDIR, mappingname)
    with open(mappingfile, 'rt') as infile:
        return infile.read()

def get_clean_mapping(mappingname):
    if '.txt' not in mappingname:
        mappingname += '.txt'
    
    mappingfile = os.path.join(MAPPINGDIR, mappingname)
    with open(mappingfile, 'rt') as infile:
        lines = infile.readlines()
        lines = [re.sub(r'#.*$', '', line) for line in lines]
        lines = [line.strip() for line in lines]
        lines = [line for line in lines if len(line) > 0]
        return ' '.join(lines) + '\n'


def send_mapping(serialPortName, mappingname):
    try:
        mappingStr = get_mapping(mappingname)
    except FileNotFoundError as e:
        print("couldn't open " + e.filename)
        sys.exit(2)

    with serial.Serial(serialPortName, 115200, timeout=1) as ser:
        ser.write(mappingStr.encode('utf-8'))
        res = ser.read(2000)

        print('response:')
        print(res.decode('utf-8'), end='')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('USAGE: ' + sys.argv[0] + ' <serialport> <mappingname>\n\n   e.g. ' + sys.argv[0] + ' COM8 hydra0', file=sys.stderr)
        sys.exit(1)

    send_mapping(sys.argv[1], sys.argv[2])
