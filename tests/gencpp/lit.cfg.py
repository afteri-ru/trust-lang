import os
import lit.formats

config.name = "gencpp-lit"
config.test_format = lit.formats.ShTest(True)
config.suffixes = ['.ast']
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.test_source_root, 'Output')

# Path to gencpp_from_ast binary
# When run via CMake, the binary is in build/gencpp_from_ast
_build_dir = os.path.abspath(os.path.join(config.test_source_root, '..', '..', 'build'))
_gencpp_cli = os.path.join(_build_dir, 'gencpp_from_ast')

# Normalize path
_gencpp_cli = os.path.normpath(_gencpp_cli)

lit_config.note(f"Using gencpp_from_ast: {_gencpp_cli}")

config.substitutions.append(('%trans', _gencpp_cli))

# C++ compiler for syntax checking
_cxx = os.environ.get('CXX', 'g++')
config.substitutions.append(('%cxx', _cxx))