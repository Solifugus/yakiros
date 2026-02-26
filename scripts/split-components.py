#!/usr/bin/env python3
"""
Split combined component file into individual .toml files

This script takes the examples/components.toml file (which contains multiple
component declarations) and splits them into individual .toml files in
examples/split/ directory.
"""

import os
import sys
import re

def main():
    input_file = "examples/components.toml"
    output_dir = "examples/split"

    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found")
        return 1

    # Create output directory
    os.makedirs(output_dir, exist_ok=True)

    # Read the combined file
    with open(input_file, 'r') as f:
        content = f.read()

    # Split by component markers (=== component-name.toml ===)
    components = re.split(r'# === ([^.]+)\.toml ===', content)

    components_created = 0

    # Process each component (skip the header)
    for i in range(1, len(components), 2):
        if i + 1 >= len(components):
            break

        component_name = components[i]
        component_content = components[i + 1].strip()

        # Skip empty components
        if not component_content or not component_content.startswith('[component]'):
            continue

        # Clean up the component content
        lines = component_content.split('\n')
        clean_lines = []
        for line in lines:
            # Skip empty lines at start and comment-only sections
            if line.strip() and not line.startswith('# ─────'):
                clean_lines.append(line)
            elif clean_lines:  # Keep empty lines after content starts
                clean_lines.append(line)

        # Write to individual file
        output_file = os.path.join(output_dir, f"{component_name}.toml")
        with open(output_file, 'w') as f:
            f.write('\n'.join(clean_lines).strip() + '\n')

        print(f"Created: {output_file}")
        components_created += 1

    print(f"Successfully split {components_created} components into {output_dir}/")
    return 0

if __name__ == "__main__":
    sys.exit(main())