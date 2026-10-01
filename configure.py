# vim: set sts=2 ts=8 sw=2 tw=99 et:
import sys

try:
  from ambuild2 import run
except ImportError:
  sys.stderr.write('AMBuild 2 is required to build CSVILKA.\n')
  sys.exit(1)

if getattr(run, 'CURRENT_API', '2.1').startswith('2.1'):
  sys.stderr.write('AMBuild 2.2 or newer is required to build CSVILKA.\n')
  sys.exit(1)

parser = run.BuildParser(sourcePath=sys.path[0], api='2.2')
parser.options.add_argument('--enable-debug', action='store_const', const='1', dest='debug',
                            help='Enable debugging symbols')
parser.options.add_argument('--enable-optimize', action='store_const', const='1', dest='opt',
                            help='Enable compiler optimization')
parser.Configure()
