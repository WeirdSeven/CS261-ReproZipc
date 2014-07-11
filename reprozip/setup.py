import os
from setuptools import setup, Extension
import sys


# List the source files
sources = ['pytracer.c', 'tracer.c', 'database.c', 'ptrace_utils.c', 'utils.c']
# They can be found under native/
sources = [os.path.join('native', n) for n in sources]


# Setup the libraries
libraries = ['sqlite3', 'rt']


# Build the C module
pytracer = Extension('reprozip._pytracer',
                     sources=sources,
                     libraries=libraries)

with open('README.rst') as fp:
    description = fp.read()
req = [
    'PyYAML',
    'rpaths>=0.4']
if sys.version_info < (2, 7):
    req.append('argparse')
setup(name='reprozip',
      version='0.2.1',
      ext_modules=[pytracer],
      packages=['reprozip', 'reprozip.tracer'],
      entry_points={'console_scripts': [
          'reprozip = reprozip.main:main']},
      install_requires=req,
      description="Linux tool enabling reproducible experiments (packer)",
      author="Remi Rampin, Fernando Chirigati, Dennis Shasha, Juliana Freire",
      author_email='reprozip-users@vgc.poly.edu',
      maintainer="Remi Rampin",
      maintainer_email='remirampin@gmail.com',
      url='http://github.com/ViDA-NYU/reprozip',
      long_description=description,
      license='BSD',
      keywords=['reprozip', 'reprounzip', 'reproducibility', 'provenance',
                'vida', 'nyu'],
      classifiers=[
          'Development Status :: 3 - Alpha',
          'Intended Audience :: Science/Research',
          'License :: OSI Approved :: BSD License',
          'Operating System :: POSIX :: Linux',
          'Programming Language :: C',
          'Topic :: Scientific/Engineering',
          'Topic :: System :: Archiving'])
