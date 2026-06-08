import sys, re
# replicate CDE programs/localized/util/merge.c: substitute %|nls-N-#tag#|
# with message N from a .tmsg catalog source.
def parse_tmsg(path):
    msgs = {}
    cur = None; buf = []
    for raw in open(path, 'r', errors='replace'):
        line = raw.rstrip('\n')
        if line.startswith('$'):        # $ comment / $set / $quote — ignore
            continue
        if cur is None:
            m = re.match(r'^(\d+)\s(.*)$', line)
            if not m:
                continue
            cur = int(m.group(1)); buf = [m.group(2)]
        else:
            buf.append(line)
        # continuation: line ends with a single trailing backslash
        if buf and buf[-1].endswith('\\') and not buf[-1].endswith('\\\\'):
            buf[-1] = buf[-1][:-1]      # strip the continuation backslash
            continue
        # message complete — join and process escapes
        text = '\n'.join([]) if False else ''.join(buf) if False else None
        joined = ''.join(buf)
        out = []
        i = 0
        while i < len(joined):
            c = joined[i]
            if c == '\\' and i+1 < len(joined):
                n = joined[i+1]
                out.append({'n':'\n','t':'\t','\\':'\\','"':'"'}.get(n, n))
                i += 2
            else:
                out.append(c); i += 1
        msgs[cur] = ''.join(out)
        cur = None; buf = []
    return msgs

def expand(template, msgs):
    def repl(m):
        n = int(m.group(1))
        return msgs.get(n, "....Missing message #%d" % n)
    return re.sub(r'%\|nls-(\d+)-[^|]*\|', repl, template)

tmsg, tmpl = sys.argv[1], sys.argv[2]
msgs = parse_tmsg(tmsg)
sys.stdout.write(expand(open(tmpl,'r',errors='replace').read(), msgs))
