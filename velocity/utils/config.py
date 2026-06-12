import importlib.util
import argparse

parser = argparse.ArgumentParser(description="Generate C and S header files from a Velocity configuration file.")
parser.add_argument("input_file", nargs="?", default="velocity/hw/velocity_arch.py", help="Path to the input Python file")
args = parser.parse_args()
input_file = args.input_file

# Read the input Python file
C_header_file = 'velocity/sw/runtime/include/velocity_arch.h'
S_header_file = 'velocity/sw/runtime/include/velocity_arch.inc'

spec = importlib.util.spec_from_file_location('velocity_arch_config', input_file)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
arch = module.VelocityArch()

attributes = {}
for attr_name, attr_value in vars(arch).items():
    if isinstance(attr_value, bool):
        attributes[attr_name] = int(attr_value)
    elif isinstance(attr_value, int):
        attributes[attr_name] = attr_value

# Write the output C header file
with open(C_header_file, 'w') as file:
    file.write('#ifndef VELOCITYARCH_H\n')
    file.write('#define VELOCITYARCH_H\n\n')
    
    for attr_name, attr_value in attributes.items():
        # Convert attribute name to uppercase and prefix with 'ARCH_'
        define_name = f'ARCH_{attr_name.upper()}'
        file.write(f'#define {define_name} {attr_value}\n')
    
    file.write('\n#endif // VELOCITYARCH_H\n')

print(f'Header file "{C_header_file}" generated successfully.')

# Write the output S header file
with open(S_header_file, 'w') as file:
    file.write('#ifndef VELOCITYARCH_H\n')
    file.write('#define VELOCITYARCH_H\n\n')
    
    for attr_name, attr_value in attributes.items():
        # Convert attribute name to uppercase and prefix with 'ARCH_'
        define_name = f'ARCH_{attr_name.upper()}'
        file.write(f'.set {define_name}, {attr_value}\n')
    
    file.write('\n#endif // VELOCITYARCH_H\n')

print(f'Header file "{S_header_file}" generated successfully.')
