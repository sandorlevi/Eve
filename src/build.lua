util = require("util")
Set = require("set").Set
math = require("math")
parser = require("parser")
db = require("db")
errors = require("error")

local makeNode = parser.makeNode
local DefaultNodeMeta = parser.DefaultNodeMeta

function shallowcopy(orig)
    local copy = {}
    for k, v in pairs(orig) do
       copy[k] = v
    end
    return copy
end

function flat_print_table(t)
   if type(t) == "table" then
     local result = ""
     for k, v in pairs(t) do
        if not (k == nil) then result = result .. " " .. tostring(k) .. ":" end
        if not (v == nil) then result = result .. tostring(v) end
     end
     return result
   end
   return tostring(t)
end

function recurse_print_table(t)
   if t == nil then return nil end
   local result = ""
   for k, v in pairs(t) do
      result = result .. " " .. tostring(k) .. ":"
     if (type(v) == "table") then
        result = result .. "{" .. recurse_print_table(v) .. "}"
     else
        result = result .. tostring(v)
     end
   end
   return result
end

function cnode(n, name, args, db)
   -- create an edge between the c node and the parse node
   local id = generate_uuid()
   insert_edb(db, id, sstring("type"), sstring(name), 1)  
   for a, v in pairs(args) do          
      if type(v) == "table" then
         for _, v2 in ipairs(v) do           
           insert_edb(db, id, sstring(a), v2, 1)
         end
      else  
        insert_edb(db, id, sstring(a), v, 1)
      end
   end
   return id
end




function translate_value(x)
   if type(x) == "table" then
      local ct = x.constantType
      if ct == "string" then
         return sstring(x.constant)
      end
      if ct == "number" then
         return snumber(x.constant)
      end

      if ct == "boolean" then
         if (x.constant == "true") then
           return sboolean(true)
         end
         if (x.constant == "false") then
           return sboolean(false)
         end
      end

      if ct == "uuid" then
         print ("dont be passing me a textual uuid..for shame")
      end

      if ct == "none" then
         return snone()
      end

      print ("i couldn't figure out this value", flat_print_table(x))
      return x
   end
   return x
end

function deepcopy(orig)
   local orig_type = type(orig)
   local copy
   if orig_type == 'table' then
       copy = {}
       for orig_key, orig_value in next, orig, nil do
              copy[deepcopy(orig_key)] = deepcopy(orig_value)
       end
    else -- number, string, boolean, etc
       copy = orig
    end
   return copy
end

-- end of util

function empty_env()
   return {alloc=0, freelist = {}, registers = {}, permanent = {}, maxregs = 0, ids = {}}
end

function variable(x)
   return type(x) == "table" and x.type == "variable"
end


function free_register(n, env, e)
   if env.permanent[e] == nil and env.registers[e] then
     if env.freelist[env.registers[e]] then
       error(string.format("Attempt to double-free register: %s for variable %s", env.registers[e], e))
     end
     env.freelist[env.registers[e]] = true
     env.registers[e] = nil
     while(env.freelist[env.alloc-1]) do
        env.alloc = env.alloc - 1
        env.freelist[env.alloc] = nil
     end
   end
end

function allocate_register(n, env, e)
   -- if not variable(e) or env.registers[e] then  return end
   if env.registers[e] then
      error(string.format("Attempt to double-allocate register for: %s in register %s", e, env.registers[e]))
   end
   local slot = env.alloc
   for index,value in ipairs(env.freelist) do
      slot = math.min(slot, index)
   end
   if slot == env.alloc then env.alloc = env.alloc + 1
   else env.freelist[slot] = nil end
   env.registers[e] = slot
   env.maxregs = math.max(env.maxregs, slot)
   return slot
end

head_to_tail_counter = 0

function allocate_temp(node)
  head_to_tail_counter =  head_to_tail_counter + 1
  local variable = setmetatable({type = "variable",
                                 line = node.line,
                                 offset = node.offset,
                                 id = util.generateId()}, DefaultNodeMeta)
  variable.name= "temp_" .. head_to_tail_counter
  variable.generated = true
  node.query.variables[#node.query.variables + 1] = variable
  return variable
end

function read_lookup(n, env, x)
   if variable(x) then
      local r = env.registers[x]
      if not r then
         r = allocate_register(n, env, x)
         env.registers[x] = r
      end
      if not n.registers then n.registers = {} end
      if x and not r then error("AHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH read " .. tostring(x)) end
      if x then n.registers[x.id] = "r" .. r end
      return sregister(r)
   end
   return translate_value(x)
end

function write_lookup(n, env, x)
   -- can't be a constant or unbound
   local r = env.registers[x]
   if r then
     --free_register(n, env, x)
   else
     r = allocate_register(n, env, x)
     env.registers[x] = r
   end
   if not n.registers then n.registers = {} end
   if x then n.registers[x.id] = "w" .. r end
   return sregister(r)
end


function bound_lookup(bindings, x)
   if variable(x) then
         return bindings[x]
   end
   return x
end

function set_to_read_array(n, env, x)
   local out = {}
   for k, v in pairs(x) do
       if not k.cardinal then
         out[#out+1] = read_lookup(n, env, k)
       end
   end
   return out
end

function set_to_write_array(n, env, x)
   local out = {}
   for k, v in pairs(x) do
      out[#out+1] = write_lookup(n, env, k)
   end
   return out
end

function list_to_read_array(n, env, x)
   local out = {}
   for _, v in ipairs(x) do
      out[#out+1] = read_lookup(n, env, v)
   end
   return out
end

function list_to_write_array(n, env, x)
   local out = {}
   for _, v in ipairs(x) do
      out[#out+1] = write_lookup(n, env, v)
   end
   return out
end


function translate_subagg(n, bound, down, db)
  local pass = allocate_temp(n)
  bound[pass] = true

  function tail (bound)
       local env, rest = down(bound)
       return env, cnode(n, "subaggtail", 
                          {next = rest,
                           groupings = set_to_read_array(n, env, n.groupings or {}),
                           provides = set_to_read_array(n, env, n.provides),
                           pass = read_lookup(n, env, pass)},
                         db)
  end

  local env, rest = walk(n.nodes, nil, bound, tail, db)


  c = cnode(n, "subagg", 
                   {next = rest,
                    projection = set_to_read_array(n, env, n.projection),
                    groupings = set_to_read_array(n, env, n.groupings or {}),
                    pass = write_lookup(n, env, pass)},
                 db)

  return env, c
end

function translate_subproject(n, bound, down, db)
   local t = n.nodes
   local env, rest, fill, c
   local pass = allocate_temp(n)
   local leg_bound = shallowcopy(bound)
   bound[pass] = true

   local provides = Set:new()
   for term, _ in pairs(n.provides) do
     if not term.cardinal and not bound[term] then
       provides:add(term)
       leg_bound[term] = true
     end
   end

   env, rest = down(leg_bound)

   local saveids = env.ids
   env.ids = {}
   function tail (bound)
     return env, cnode(n, "subtail",
                             {provides=set_to_read_array(n, env, provides),
                             pass=read_lookup(n, env, pass)},
                       db)

   end

   env, fill = walk(n.nodes, nil, bound, tail, db)
   if #n.nodes == 0 then
     -- with no nodes in the subproject and no ids in need of generation, we just omit it entirely.
     -- When the compiler is more intelligent and its second pass is less hacked together, we can move this there.
     if provides:length() == 0 then
       env.ids = saveids
       return env, rest
     end

     for term in pairs(provides) do
       if not bound[term] then
         env.ids[term] = true
       end
     end
   end

   c = cnode(n, "sub",
              {arm = fill,
               next = rest,  
               projection = set_to_read_array(n, env, n.projection),
               provides = set_to_read_array(n, env, provides),
               pass = write_lookup(n, env, pass),
               ids = set_to_write_array(n, env, env.ids),
               id_collapse = true},
            db)

   env.ids = saveids
   return env, c
end

function translate_object(n, bound, down, db)
   local e = n.entity
   local a = n.attribute
   local v = n.value
   local sig = "EAV"
   local ef = read_lookup
   local af = read_lookup
   local vf = read_lookup

   if not bound_lookup(bound, e) then
       sig = "eAV"
       bound[e] = true
       ef = write_lookup
   end
   if not bound_lookup(bound, a) then
       sig = string.sub(sig, 0, 1) .. "aV"
       bound[a] = true
       af = write_lookup
   end
   if not bound_lookup(bound, v) then
       sig = string.sub(sig, 0, 2) .. "v"
       bound[v] = true
       vf = write_lookup
   end

   local env, c = down(bound)

   -- the nodes arguments are all arrays, so translate
   local scope = {}
   for k, v in pairs(n.scopes) do
        scope[#scope + 1] = k
   end

   return env, cnode(n, "scan",
                      {sig = sig,
                       next = c,
                       scope = scope,
                       e = ef(n, env, e),
                       a = af(n, env, a),
                       v = vf(n, env, v)},
                    db)
 end


function translate_mutate(n, bound, down, db)
   local e = n.entity
   local a = n.attribute
   local v = n.value

   local gen = (variable(e) and not bound[e])
   if (gen) then bound[e] = true end

   local env, c = down(bound)
   local operator = n.operator

   -- the nodes arguments are all arrays, so translate
   local scope = {}
   for k, v in pairs(n.scopes) do
        scope[#scope + 1] = read_lookup(n, env, k)
   end

   c = cnode(n, operator,
                        {next = c, 
                         scope = scope,
                         mutateType = n.mutateType,
                         e = read_lookup(n, env,e),
                         a = read_lookup(n, env,a),
                         v = read_lookup(n, env,v)},
               db)

   if gen then
     env.ids[e] = read_lookup(n, env, e)
   end
   return env, c
end

function translate_not(n, bound, down, db)
   local env
   local arm = {}
   local flag = allocate_temp(n)
   tail_bound = shallowcopy(bound)

   local env, c = down(tail_bound)
   local orig_perm = shallowcopy(env.permanent)
   local bot = cnode(n, "choosetail", 
                          {pass = read_lookup(n, env, flag)},
                          db)

   local arm_bottom = function (bound)
        return env, bot
   end

   for n, _ in pairs(env.registers) do
         env.permanent[n] = true
   end
   env, arm = walk(n.queries[1].unpacked, nil, shallowcopy(bound), arm_bottom, db)
   return env, cnode(n, "not", {next = c, arm = {arm}, pass = read_lookup(n, env, flag)}, db)
end


-- looks alot like union
function translate_choose(n, bound, down, db)
   local env
   local arm = {}
   local flag = allocate_temp(n)

   local tail_bound = shallowcopy(bound)
   for _, v in pairs(n.outputs) do
      tail_bound[v] = true
   end

   local env, c = down(tail_bound)
   local orig_perm = shallowcopy(env.permanent)

   arm[1] = c

   local bot = cnode(n, "choosetail",
                          {next = c, pass = read_lookup(n, env, flag)},
                          db)

   local arm_bottom = function (bound)
        return env, bot
   end

   for n, _ in pairs(env.registers) do
         env.permanent[n] = true
   end

   for _, v in pairs(n.queries) do
        env, c2 = walk(v.unpacked, nil, shallowcopy(bound), arm_bottom, db)
        arm[#arm+1] = c2
   end

   env.permanent = orig_perm
   return env, cnode(n, "choose", {arm = arm, pass = read_lookup(n, env, flag)}, db)
end

function translate_union(n, bound, down, db)
   local heads
   local c2
   local arm = {}
   tail_bound = shallowcopy(bound)

   for _, v in pairs(n.outputs) do
      tail_bound[v] = true
   end

   local env, c = down(tail_bound)

   local arm_bottom = function (bound)
                         return env, c
                      end

   local orig_perm = shallowcopy(env.permanent)
   for n, _ in pairs(env.registers) do
      env.permanent[n] = true
   end

   for _, v in pairs(n.queries) do
      local c2
      env, c2 = walk(v.unpacked, nil, shallowcopy(bound), arm_bottom, db)
      arm[#arm+1] = c2
   end
   env.permanent = orig_perm

   return env, cnode(n, "fork", nil, arm , {}, db)
end


function translate_expression(n, bound, down, edb)
  local signature = db.getSignature(n.bindings, bound)
  local schema = db.getSchema(n.operator, signature)
  if not schema then
    errors.unknownExpressionSignature(context, n, signature, db.getSchemas(n.operator))
    return down(bound)
  end

  local args, fields = db.getArgs(schema, n.bindings)
  for _, term in ipairs(args) do
    bound[term] = true
  end
  local env, c = down(bound)

   -- Tack variadic arg vector onto the end
   local variadic
   if args["..."] then
     variadic = list_to_read_array(n, env, args["..."])
   end

   local groupings
   if n.groupings then
     groupings = set_to_read_array(n, env, n.groupings)
   end

   local nodeArgs = {}
   for ix, field in ipairs(fields) do
     if schema.signature[field] == db.OUT then
       nodeArgs[field] = write_lookup(n, env, args[ix])
     else
       nodeArgs[field] = read_lookup(n, env, args[ix])
     end
   end

   if variadic then
       nodeArgs.variadic = variadic
   end
   if groupings then
       nodeArgs.groupings = groupings
   end

   nodeArgs.next = c
   return env, cnode(n, schema.name or n.operator, nodeArgs, edb)
end

function walk(graph, key, bound, tail, db)
   local d, down
   local nk = next(graph, key)
   if not nk then
      return tail(bound)
   end

   local n = graph[nk]

   d = function (bound)
         return walk(graph, nk, bound, tail, db)
       end

   if (n.type == "union") then
      return translate_union(n, bound, d, db)
   end
   if (n.type == "mutate") then
      return translate_mutate(n, bound, d, db)
   end
   if (n.type == "object") then
      return translate_object(n, bound, d, db)
   end
   if (n.type == "subproject") then
     if n.kind == "aggregate" then
       return translate_subagg(n, bound, d, db)
     else
       return translate_subproject(n, bound, d, db)
     end
   end
   if (n.type == "choose") then
      return translate_choose(n, bound, d, db)
   end
   if (n.type == "expression") then
      return translate_expression(n, bound, d, db)
   end
   if (n.type == "not") then
      return translate_not(n, bound, d, db)
   end

   print ("ok, so we kind of suck right now and only handle some fixed patterns",
         "type", n.type,
         "entity", flat_print_table(e),
         "attribute", flat_print_table(a),
         "value", flat_print_table(v))
end


function build(queryGraph, db)
  -- put the error path into env if we still care at all
  bid = generate_uuid()
  local tailf = function(b)
    return empty_env(), cnode(queryGraph, "terminal", {}, db)
  end
  local env, program = walk(queryGraph.unpacked, nil, {}, tailf, db)

  insert_edb(db, bid, sstring("name"), sstring(queryGraph.name), 1)
  insert_edb(db, bid, sstring("tag"), sstring(block), 1)
  insert_edb(db, bid, sstring("regs"), snumber(env.maxregs), 1)
  insert_edb(db, bid, sstring("start"), program, 1)
  return program, env.maxregs
end

------------------------------------------------------------
-- Parser interface
------------------------------------------------------------

return {
  build = build
}
