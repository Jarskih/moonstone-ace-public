"""jsonschema_lite.py -- a small JSON Schema subset validator (no third-party package needed; ROADMAP 9.8a1).

Supported keywords: type (name or list), enum, const, properties, patternProperties, required, additionalProperties (bool or
schema), propertyNames (pattern), items (schema), minItems, maxItems, minProperties, minimum, maximum, minLength, maxLength,
pattern, oneOf, anyOf, allOf, $ref (local `#/definitions/...`).  Everything else (description, title, $schema, $id, format ...)
is ignored.

    validate(instance, schema)  -> list of error strings ("path: message"); empty = valid
"""
import re

_TYPES = {
    'object': lambda v: isinstance(v, dict),
    'array': lambda v: isinstance(v, list),
    'string': lambda v: isinstance(v, str),
    'integer': lambda v: isinstance(v, int) and not isinstance(v, bool),
    'number': lambda v: isinstance(v, (int, float)) and not isinstance(v, bool),
    'boolean': lambda v: isinstance(v, bool),
    'null': lambda v: v is None,
}


def _resolve(root, ref):
    if not ref.startswith('#/'):
        raise ValueError('only local refs: %s' % ref)
    node = root
    for part in ref[2:].split('/'):
        node = node[part]
    return node


def _check(inst, schema, root, path, errs):
    if '$ref' in schema:
        _check(inst, _resolve(root, schema['$ref']), root, path, errs)
        return
    if 'type' in schema:
        types = schema['type'] if isinstance(schema['type'], list) else [schema['type']]
        if not any(_TYPES[t](inst) for t in types):
            errs.append('%s: expected %s, got %s' % (path or '$', '/'.join(types), type(inst).__name__))
            return
    if 'const' in schema and not (inst == schema['const'] and type(inst) == type(schema['const'])):
        errs.append('%s: must be %r' % (path or '$', schema['const']))
    if 'enum' in schema and inst not in schema['enum']:
        errs.append('%s: %r not in %r' % (path or '$', inst, schema['enum']))
    if isinstance(inst, (int, float)) and not isinstance(inst, bool):
        if 'minimum' in schema and inst < schema['minimum']:
            errs.append('%s: %r below %r' % (path or '$', inst, schema['minimum']))
        if 'maximum' in schema and inst > schema['maximum']:
            errs.append('%s: %r above %r' % (path or '$', inst, schema['maximum']))
    if isinstance(inst, str):
        if 'minLength' in schema and len(inst) < schema['minLength']:
            errs.append('%s: shorter than %d' % (path or '$', schema['minLength']))
        if 'maxLength' in schema and len(inst) > schema['maxLength']:
            errs.append('%s: longer than %d' % (path or '$', schema['maxLength']))
        if 'pattern' in schema and not re.search(schema['pattern'], inst):
            errs.append('%s: %r does not match %s' % (path or '$', inst, schema['pattern']))
    if isinstance(inst, list):
        if 'minItems' in schema and len(inst) < schema['minItems']:
            errs.append('%s: fewer than %d items' % (path or '$', schema['minItems']))
        if 'maxItems' in schema and len(inst) > schema['maxItems']:
            errs.append('%s: more than %d items' % (path or '$', schema['maxItems']))
        if 'items' in schema:
            for i, v in enumerate(inst):
                _check(v, schema['items'], root, '%s[%d]' % (path, i), errs)
    if isinstance(inst, dict):
        if 'minProperties' in schema and len(inst) < schema['minProperties']:
            errs.append('%s: fewer than %d properties' % (path or '$', schema['minProperties']))
        for r in schema.get('required', []):
            if r not in inst:
                errs.append('%s: missing "%s"' % (path or '$', r))
        props = schema.get('properties', {})
        pprops = schema.get('patternProperties', {})
        addl = schema.get('additionalProperties', True)
        for k, v in inst.items():
            sub = '%s.%s' % (path, k) if path else k
            if 'propertyNames' in schema and 'pattern' in schema['propertyNames'] and not re.search(schema['propertyNames']['pattern'], k):
                errs.append('%s: bad property name %r' % (path or '$', k))
            matched = False
            if k in props:
                matched = True
                _check(v, props[k], root, sub, errs)
            for pat, ps in pprops.items():
                if re.search(pat, k):
                    matched = True
                    _check(v, ps, root, sub, errs)
            if not matched:
                if addl is False:
                    errs.append('%s: unknown property "%s"' % (path or '$', k))
                elif isinstance(addl, dict):
                    _check(v, addl, root, sub, errs)
    if 'oneOf' in schema:
        n = 0
        for s in schema['oneOf']:
            e = []
            _check(inst, s, root, path, e)
            n += not e
        if n != 1:
            errs.append('%s: matches %d of the oneOf alternatives (need exactly 1)' % (path or '$', n))
    if 'anyOf' in schema:
        ok = False
        for s in schema['anyOf']:
            e = []
            _check(inst, s, root, path, e)
            ok = ok or not e
        if not ok:
            errs.append('%s: matches none of the anyOf alternatives' % (path or '$'))
    for s in schema.get('allOf', []):
        _check(inst, s, root, path, errs)


def validate(instance, schema):
    errs = []
    _check(instance, schema, schema, '', errs)
    return errs
