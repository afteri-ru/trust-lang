import os
import lit.formats

config.name = "gencpp-lit"
config.test_format = lit.formats.ShTest(True)
config.suffixes = ['.ast']
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = os.path.join(config.test_source_root, 'Output')

# Load CMake-generated settings from lit.site.cfg.py if it exists
lit_cfg_py = os.path.join(config.test_source_root, 'lit.site.cfg.py')
if os.path.exists(lit_cfg_py):
    lit_config.load_config(config, lit_cfg_py)
else:
    lit_config.fatal(f"lit.site.cfg.py not found at {lit_cfg_py}")
