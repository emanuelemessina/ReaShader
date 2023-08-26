import os
import glob

# Copyright and License Information
copyright_text = '''\
/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/
'''

# List of file extensions to process
file_extensions = ['cpp', 'h', 'scss', 'js']

# Function to insert copyright and license information
def insert_copyright_header(file_path, header):
    with open(file_path, 'r') as f:
        content = f.read()

    with open(file_path, 'w') as f:
        f.write(header + '\n' + content)

# Process files in the directory and subdirectories
def process_directory(root_dir):
     for extension in file_extensions:
        pattern = os.path.join(root_dir, '**', f'*.{extension}')
        files = glob.glob(pattern, recursive=True)
        for file_path in files:
            print(file_path)
            insert_copyright_header(file_path, copyright_text)


# Specify the directory where your code files are located
code_directory = 'D:\\Library\\Coding\\GitHub\\_Reaper\\ReaShader\\source\\reashader\\rsui\\frontend'

# Process files in the specified directory
process_directory(code_directory)

print('Copyright and license information inserted into code files.')
