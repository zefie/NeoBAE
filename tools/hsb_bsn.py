import os, argparse

BEPF_String = b'BEPF\x00\x00\x00\x01\x00\x00\x00\x00\x07example'
parser = argparse.ArgumentParser(
    description='Convert HSB to/from BSN',
    formatter_class=argparse.RawDescriptionHelpFormatter,
    epilog=__doc__
)

parser.add_argument('input', help='Input BSN/HSB file')
parser.add_argument('output', help='Output HSB/BSN file')
args = parser.parse_args()

# Read the file content if it exists
if os.path.exists(args.input):
    with open(args.input, "rb") as file:
        content = file.read()
else:
    content = b""

# Check if BEPF_String exists in the file
if BEPF_String in content:
    # Remove BEPF_String from the file
    content = content.replace(BEPF_String, b"")
    with open(args.output, "wb") as file:
        file.write(content)
else:
    # Append BEPF_String to the file
    with open(args.output, "wb") as file:
        file.write(content)
        file.write(BEPF_String)