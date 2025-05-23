// This software is in the public domain.

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifndef BILLGATES
#include <unistd.h>
#endif
#include "chub.h" 
#include "situations.h"
#include "talloc.c"

enum {
	TINT=0,
    TSYMBOL,
    TPRIMITIVE,
    TFUNCTION,
    TSPECIAL,
    TENV,
    
	TFISH,
	TSOUP,
	TTANK,
	TBOAT

};

// Subtypes for TSPECIAL
enum {
    TNIL = 1,
    TDOT,
    TCPAREN,
    TTRUE,
};

struct Obj;

// Typedef for the primitive function.
typedef struct Obj *Primitive(struct Obj *env, struct Obj*args, struct Obj*par);

// The object type
typedef struct Obj {
    // The first word of the object represents the type of the object. Any code that handles object
    // needs to check its type first, then access the following union members.
    int type;

    // Object values.
    union {
        // Int
        int value;
        // Cell
        struct {
            struct Obj *car;
            struct Obj *cdr;
            //for tank struct Obj *env;
        };
        // Symbol
        char name[1];
        // Primitive
        Primitive *fn;
        // Function or Macro
        struct {
            struct Obj *params;
            struct Obj *body;
            struct Obj *env;
        };
        // Subtype for special type
        int subtype;
        // Environment frame. This is a linked list of association lists
        // containing the mapping from symbols to their value.
        struct {
            struct Obj *vars;
            struct Obj *up;
        };
    };
} Obj;

// Constants
//static Obj *Nil;
#define Nil 0
static Obj *Dot;
static Obj *Cparen;
static Obj *True;

// The list containing all symbols. Such data structure is traditionally called the "obarray", but I
// avoid using it as a variable name as this is not an array but a list.
static Obj *Symbols;

static void error(const char *fmt, ...) ;//__attribute((noreturn));

//constructors
static Obj *alloc(int type, size_t size, Obj*par) {
 size += offsetof(Obj, value);
 Obj *obj = (Obj*)talloc(size, par);
 obj->type = type;
 return obj;
}

static Obj *make_int(int value,Obj*par) {
 Obj *r = alloc(TINT, sizeof(int),par);
 r->value = value;
 return r;
}
static Obj *make_intd(double x,Obj*par) {
 x =((x)>=0?(int)((x)+0.5):(int)((x)-0.5));
 Obj *r = alloc(TINT, sizeof(int),par);
 r->value = x;
 return r;
}

static Obj *make_symbol(const char *name,Obj*par) {
 Obj *sym = alloc(TSYMBOL, strlen(name) + 1,par);
 strcpy(sym->name, name);
 return sym;
}

static Obj *make_primitive(Primitive *fn,Obj*par) {
    Obj *r = alloc(TPRIMITIVE, sizeof(Primitive *),par);
    r->fn = fn;
    return r;
}

static Obj *make_function(Obj *params, Obj *body, Obj *env,Obj*par) {
    Obj *r = alloc(TFUNCTION, sizeof(Obj *) * 3,par);
    r->params = params;
    r->body = body;
    r->env = env;
    return r;
}

static Obj *make_special(int subtype, Obj*par) {
 Obj *r = (Obj*)alloc(TSPECIAL,sizeof(void*)*2,par);
 //r->type = TSPECIAL;
 r->subtype = subtype;
 return r;
}

struct Obj *make_env(Obj *vars, Obj *up,Obj*par) {
 Obj *r = alloc(TENV, sizeof(Obj *) * 2,par);
 r->vars = vars;
 r->up = up;
 return r;
}

static Obj *cons(int type, Obj *car, Obj *cdr,Obj*par) {
 Obj *cell = alloc(type, sizeof(Obj *) * 2,par);
 cell->car = car;
 cell->cdr = cdr;
 return cell;
}

static Obj *fish(Obj *car, Obj *cdr,Obj*par) {
    return cons(TFISH, car, cdr,par);
}
static Obj *soup(Obj *car, Obj *cdr,Obj*par) {
    return cons(TSOUP, car, cdr,par);
}
static Obj *tank(Obj *car, Obj *cdr,Obj*par) {
    return cons(TTANK, car, cdr,par);
}
static Obj *boat(Obj *car, Obj *cdr,Obj*par) {
    return cons(TBOAT, car, cdr,par);
}

//Returns [[x.y].a]
static Obj *acons(Obj*x,Obj*y,Obj*a,Obj*par) {
 return tank(tank(x,y,par),a,par);
}
//parser
static Obj *readhui(void);

static void error(const char *fmt, ...) {
 va_list ap;
 va_start(ap, fmt);
 vfprintf(stdout, fmt, ap);
 fprintf(stdout, "\n");
 va_end(ap);
 exit(1);
}

static int peek(void) {
 int c = getchar();
 ungetc(c, stdin);
 return c;
}

// Skips the input until newline is found. Newline is one of \r, \r\n or \n.
static void skip_line(void) {
    for (;;) {
        int c = getchar();
       
        if (c == EOF || c == '\n')
            return;
        if (c == '\r') {
            if (peek() == '\n')
                getchar();
            return;
        }
    }
}

// readhuis a list. Note that '(' has alreadhuiy been readhui.
static Obj *readhui_list(int type) {
    Obj *obj = readhui();
    if (obj==0) //return 0;
        error("Unclosed parenthesis");
    if (obj == Dot)
        error("Stray dot");
    if (obj == Cparen)
        return cons(type,0,0,0);
    Obj *head, *tail;
    head = tail = cons(type,obj,0,0);

    for (;;) {
        Obj *obj = readhui();
//        if (!obj)
			
 //           return 0;//error("Unclosed parenthesis");
        if (obj == Cparen)
            return head;
        if (obj == Dot) {
            tail->cdr = readhui();
            if (readhui() != Cparen)
                error("Closed parenthesis expected after dot");
            return head;
        }
        if (obj) tail->cdr = cons(type,obj,0,0);
        else tail->cdr = cons(type,0,0,0);
        tail = tail->cdr;
    }
}

// May create a new symbol. If there's a symbol with the same name, it will not create a new symbol
// but return the existing one.
static Obj *intern(const char *name, Obj*par) {
 for (Obj *p = Symbols; p; p = p->cdr)
  if (strcmp(name, p->car->name) == 0)
   return p->car;
 Obj *sym = make_symbol(name,par);
 Symbols = tank(sym, Symbols,par);
 return sym;
}

static int readhui_number(int val) {
    while (isdigit(peek()))
        val = val * 10 + (getchar() - '0');
    return val;
}

#define SYMBOL_MAX_LEN 200

static bool sympars(char c) {
 return strchr("/~`?,':\"|+=_!@#$%^&*-", c);
}
//alright we gonna do a read without garbage collectrion
//that is talloc will do a null parent for everythting now
static Obj *readhui_symbol(char c) {
    char buf[SYMBOL_MAX_LEN + 1];
    int len = 1;
    buf[0] = c;
    while (isalnum(peek()) || sympars(peek())) {
        if (SYMBOL_MAX_LEN <= len)
            error("Symbol name too long");
        buf[len++] = getchar();
    }
    buf[len] = '\0';
    return intern(buf,0);
}

int looper = 1;
static Obj *readhui(void) {
    for (;;) {
        int c = getchar();
        //printf("char%c\n",c);
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        if (c == EOF) {
           looper = 0;
            return 0;
		}
        if (c == ';') {
            skip_line();
            continue;
        }
        if (c == '(')
            return readhui_list(TFISH);
        if (c == '{')
            return readhui_list(TSOUP);
        if (c == '[')
            return readhui_list(TTANK);
        if (c == '<')
            return readhui_list(TBOAT);
        if (c == ')' || c == '}' || c == ']' || c == '>')
            return Cparen;
        if (c == '.')
            return Dot;
        if (isdigit(c))
            return make_int(readhui_number(c - '0'),0);
        if (c == '-')
         if (isdigit(peek()))
          return make_int(-readhui_number(0),0);
         else return readhui_symbol(c);
        if (isalpha(c) || sympars(c))
            return readhui_symbol(c);
        error("Don't know how to handle %c", c);
    }
}

static void print(Obj*env,Obj*obj);
static void gutsprinter(const char* s,const char*e,Obj*env,Obj *obj) {
	printf(s);
 for (;;) {
		if (obj->car == 0) break;
  print(env,obj->car);
  if (obj->cdr == 0) break;
  if (obj->cdr->type < TFISH) {
   printf(" . ");
   print(env,obj->cdr);
   break;
  }
  printf(" ");
  obj = obj->cdr;
 }
 printf(e);
}
	
static Obj *eval(Obj*env,Obj*obj);	
// Prints the given object.
//needs symbolprinter/byter
static void print(Obj * env, Obj *obj) {
	if (obj==0) {
		printf("<>");return;}
   switch (obj->type) {
    case TINT:
     if (obj->value == 0) {
      insituate(0xFF);insituate(0);
     } else if ((obj->value&0xFF)==0xFF) {
      insituate(0xFF);insituate(242);insituate(0);
     } else insituate(obj->value);
     printf("%d", obj->value);
     return;
    case TFISH:
     insituate(0xFF);
     gutsprinter("(",")",env,obj);
     insituate(0);
     return;
    case TSOUP:
     situatier();
     gutsprinter("{","}",env,obj);
     situatend();
     insituate(0);
     return;
    case TTANK:
        gutsprinter("[","]",env,obj);
        return;
    case TBOAT:
		print(env, eval(env, obj));
        //gutsprinter("<",">",obj);
        return;
    case TSYMBOL:
     print(env, eval(env, obj));
        //printf("%s", obj->name);
        return;
    case TPRIMITIVE:
        printf("<primitive>");
        return;
    case TFUNCTION:
        printf("<function>");
        return;
    case TSPECIAL:
        if (obj == Nil)
            printf("()");
        else if (obj == True)
            printf("t");
        else
            error("Bug: print: Unknown subtype: %d", obj->subtype);
        return;
    default:
        error("Bug: print: Unknown tag type: %d", obj->type);
    }
}

static int list_length(Obj *list) {
    int len = 0;
    for (;;) {
        if (list == 0)
            return len;
        if ((list->car == 0)&&(list->cdr==0)) return len;
        /////wwhat happens in empty list head?
        if (list->type < TFISH)
            error("length: cannot handle dotted list");
        list = list->cdr;
        len++;
    }
}

//======================================================================
// Evaluator
//======================================================================

static Obj *eval(Obj*env,Obj*obj,Obj*par);

static void add_variable(Obj*env,Obj*sym,Obj*val,Obj*par) {
 env->vars = acons(sym, val, env->vars,par);
}

// Returns a newly created environment frame.
static Obj *push_env(Obj *env, Obj *vars, Obj *values,Obj*par) {
    //printf("%d %d llengs", list_length(vars), list_length(values));
 if (list_length(vars) != list_length(values))
  error("Cannot apply function: number of argument does not match");
 if (list_length(vars)==0) return env;
 Obj *map = 0;
 Obj *nev = make_env(map, env,par);
 for (Obj *p = vars, *q = values; p != 0; p = p->cdr, q = q->cdr) {
  Obj *sym = p->car;
  Obj *val = q->car;
  map = acons(sym, val, map, par);
 } nev->vars=map;
 return nev;
}

// Evaluates the list elements from head and returns the last return value.
static Obj *progn(Obj*env,Obj*list,Obj*par) {
 Obj *r = 0;
 for (Obj*lp=list;lp;lp=lp->cdr)
  r = eval(env,lp->car,par);
 return r;
}

// Evaluates all the list elements and returns their return values as a new list.
static Obj *eval_list(Obj *env, Obj *list,Obj*par) {
 Obj *head = 0;
 Obj *tail = 0;
 for (Obj *lp = list; lp; lp = lp->cdr) {
  Obj *tmp = eval(env, lp->car,par);
  if (head == 0) {
   head = tail = cons(list->type,tmp, 0,par);
  } else {
   tail->cdr = cons(list->type,tmp, 0,head);
   tail = tail->cdr;
  }
 }
 if (head == 0) return 0;
 return head;
}

static bool is_list(Obj *obj) {
  return obj == 0 || obj->type >= TFISH;
}

// Apply fn with args.
static Obj *apply(Obj*env,Obj*fn,Obj*args,Obj*par) {
	//if (args==0) return 0;
 if (!is_list(args))
  error("argument must be a list");
 Obj*res=0;
 if (fn->type == TPRIMITIVE)
  return fn->fn(env, args,par);
 if (fn->type == TFUNCTION) {
  Obj*body=fn->body;
  Obj*params=fn->params;
  Obj*eargs=eval_list(env, args,par);
  Obj*newenv=push_env(fn->env, params, eargs,par);
  res=progn(newenv, body,par);
  return res;
 } 
 error("not supported");
}

// Searches for a variable by symbol. Returns null if not found.
static Obj *find(Obj *env, Obj *sym) {
 for (Obj *p = env; p; p = p->up) {
  for (Obj *cell = p->vars; cell != Nil; cell = cell->cdr) {
   Obj *bind = cell->car;
   if (sym == bind->car)
    return bind;
  }
 }
 return 0;
}

static Obj *eval(Obj *env, Obj *obj, Obj*par) {
	if (obj==0) return 0;
 switch (obj->type) {
  case TINT:
  case TPRIMITIVE:
  case TFUNCTION:
  case TSPECIAL:
  case TTANK:
   return obj;
  case TFISH:
  case TSOUP:
   return obj;
  case TSYMBOL: {
   Obj *bind = find(env, obj);
   if (!bind)
    error("Undefined symbol: %s", obj->name);
   return bind->cdr;
  }
  case TBOAT: {        
		 Obj *fn = eval(env, obj->car,par);
		 if (fn==0) return 0;
   Obj *args = obj->cdr;
   if (fn->type != TPRIMITIVE && fn->type != TFUNCTION)
    error("The head of a list must be a function");
   return apply(env, fn, args,par);
  }
  default:
   error("Bug: eval: Unknown tag type: %d", obj->type);
 }
}


#define primmertypes(subn,tipe) \
static Obj *prim_##subn(Obj *env, Obj *list,Obj*par) { \
 if (list_length(list) != 2) \
  error("Malformed " #subn); \
 Obj *cell = eval_list(env, list,par); \
 cell->cdr = cell->cdr->car; \
 cell->type = tipe; \
 return cell; \
}
primmertypes(fish, TFISH)
primmertypes(soup, TSOUP)
primmertypes(tank, TTANK)
primmertypes(boat, TBOAT)

static Obj *prim_car(Obj *env, Obj *list,Obj*par) {
 Obj *args = eval_list(env, list,par);
 if (args->car->type < TFISH || args->cdr )
  error("Malformed car");
 return args->car->car;
}

static Obj *prim_cdr(Obj *env, Obj *list,Obj*par) {
 Obj *args = eval_list(env, list,par);
 if (args->car->type < TFISH || args->cdr)
  error("Malformed cdr");
 return args->car->cdr;
}

//tricky zone garbage-wise
static Obj *prim_setcdr(Obj*env,Obj*list,Obj*par) {
 Obj *args = eval_list(env, list,par);
 if (args->car->type < TFISH)
  error("Malformed cdr");
 if (args->cdr == 0) error("no settable");
 args->car->cdr = args->cdr->car;
 return 0;
}

static Obj *prim_setcar(Obj*env,Obj*list,Obj*par) {
 Obj *args = eval_list(env, list,par);
 if (args->car->type < TFISH)
  error("Malformed cdr");
 if (args->cdr == 0) error("no settable");
 args->car->car = args->cdr->car;
 return 0;
}

static Obj *prim_fun(Obj*env,Obj*list,Obj*par) {
 if (list->type!=TBOAT||!is_list(list->car)||list->cdr->type<TFISH)
  error("Malformed lambda");
 for (Obj *p = list->car; p; p = p->cdr) {
		if (p->car == 0) break;
  if (p->car->type != TSYMBOL)
   error("Parameter must be a symbol");
  if (!is_list(p->cdr))
   error("Parameter list is not a flat list");
 }
 Obj*car=list->car;
 Obj*cdr=list->cdr;
 return make_function(car,cdr,env,par);
}

//tricky garbage-wise
static Obj *prim_def(Obj*env,Obj*list,Obj*par) {
 if (list_length(list)!= 2||list->car->type!=TSYMBOL)
  error("Malformed setq");
 Obj *sym = list->car;
 Obj *value = eval(env,list->cdr->car,par);
 add_variable(env,sym,value,par);
 return 0;//value;
}

static Obj *prim_set(Obj *env, Obj *list,Obj*par) {
 if (list_length(list) != 2 || list->car->type != TSYMBOL)
  error("Malformed setq");
 Obj *bind = find(env, list->car);
 if (!bind)
  error("Unbound variable %s", list->car->name);
 Obj *value = eval(env, list->cdr->car,par);
 bind->cdr = value;
 return value;
}


#define primmermath(subn,summ,oper) \
 static Obj *prim_##subn(Obj *env, Obj *list,Obj*par) { \
 int sum = summ; \
 for (Obj *args = eval_list(env, list,par); args; args = args->cdr) { \
  if (args->car->type != TINT) \
   error(#subn "takes only numbers"); \
  sum = sum oper args->car->value;\
 }\
 return make_int(sum,par);\
}
primmermath(and, -1, &)
primmermath(orr, 0, |)
primmermath(xor, 0, ^)
primmermath(not, -1, ^)
primmermath(add, 0, +)
primmermath(mul, 1, *)
//primmermath(mod, -1, %)

static Obj *prim_mod(Obj*env,Obj*list,Obj*par) {
 int sum = 0;
 bool furst = true;
 for (Obj *args = eval_list(env, list,par); args; args = args->cdr) {
  if (args->car->type != TINT)
   error("mod takes only numbers");
  if (furst) sum = args->car->value;
  else sum %= args->car->value;
  furst = false;
 }
 return make_int(sum,par);
}


static Obj *prim_minus(Obj*env,Obj*list,Obj*par) {
 int sum = 0;
 bool furst = true;
 for (Obj *args = eval_list(env,list,par); args; args = args->cdr) {
  if (args->car->type != TINT)
   error("minus takes only numbers");
  if (furst) sum = args->car->value;
  else sum -= args->car->value;
  furst = false;
 }
 return make_int(sum,par);
}

static Obj *prim_divide(Obj *env, Obj *list,Obj*par) {
 int sum = 0;
 bool furst = true;
 for (Obj *args = eval_list(env,list,par); args; args = args->cdr) {
  if (args->car->type != TINT)
   error("divide takes only numbers");
  if (furst) sum = args->car->value;
  else sum /= args->car->value;
  furst = false;
 }
 return make_int(sum,par);
}

static Obj *prim_shill(Obj*env,Obj*list,Obj*par) {
 int sum = 0;
 bool furst = true;
 for (Obj *args = eval_list(env,list,par); args; args = args->cdr) {
  if (args->car->type != TINT)
   error("divide takes only numbers");
  if (furst) sum = args->car->value;
  else sum <<= args->car->value;
  furst = false;
 }
 return make_int(sum,par);
}
static Obj *prim_shirr(Obj *env, Obj *list,Obj*par) {
 int sum = 0;
 bool furst = true;
 for (Obj *args = eval_list(env, list,par); args; args = args->cdr) {
  if (args->car->type != TINT)
   error("divide takes only numbers");
  if (furst) sum = args->car->value;
  else sum >>= args->car->value;
  furst = false;
 }
 return make_int(sum,par);
}

#define TRIPART(uj) \
 if (args->car->type != TINT) error(" takes only numbers"); \
 double uj = args->car->value; \
 args = args->cdr;


static Obj *prim_euro(Obj *env, Obj *list,Obj*par) {
 if (list_length(list) != 4) error("Malformed euro");
 Obj *args = eval_list(env, list,par);
 TRIPART(a)
 TRIPART(b)
 TRIPART(c)
 TRIPART(d)
 return make_intd(d*pow(a,((double)b/(double)c)),par);
}

static Obj *prim_rand(Obj *env, Obj *list,Obj*par) {
 int sum = rand();
 for (Obj *args = eval_list(env,list,par); args; args = args->cdr) {
  if (args->car->type != TINT)
   error("* takes only numbers");
  sum %= args->car->value;
 }
 return make_int(sum,par);
}

static Obj *prim_print(Obj *env, Obj *list,Obj*par) {
 print(env, eval(env,list->car,par));    
 return 0;
}

// (if expr expr expr ...)
static Obj *prim_if(Obj *env, Obj *list,Obj*par) {
 if (list_length(list) < 2)
  error("Malformed if");
 Obj*cond=eval(env,list->car,par);
 Obj*els=list->cdr->cdr;
 if (cond) {
  if ((cond->type==TINT) && (cond->value==0))
   return els == 0 ? 0 : progn(env,els,par);
  else return eval(env, list->cdr->car,par);
 }
 return els==0?0:progn(env,els,par);
}

// (= <integer> <integer>)
static Obj *prim_num_eq(Obj*env,Obj*list,Obj*par) {
 if (list_length(list) != 2)
  error("Malformed =");
 Obj *values = eval_list(env,list,par);
 Obj *x = values->car;
 Obj *y = values->cdr->car;
 if (x->type != TINT || y->type != TINT)
  error("= only takes numbers");
 return x->value == y->value ? True : 0;
}


#define primmercomp(subn,oper) \
 static Obj *prim_##subn(Obj*env,Obj*list,Obj*par) { \
 if (list_length(list) != 2) \
  error("Malformed comparison"); \
 Obj *values = eval_list(env, list,par); \
 Obj *x = values->car; \
 Obj *y = values->cdr->car; \
 if (x->type != TINT || y->type != TINT) \
  error("= only takes numbers"); \
 return x->value oper y->value ? True : 0; \
}

primmercomp(eq, ==)
primmercomp(gt, >)
primmercomp(lt, <)


static Obj *prim_exit(Obj *env, Obj *list,Obj*par) {
 looper = 0;//exit(0);
 return 0;
}

static void add_primitive(Obj *env, const char *name, Primitive *fn) {
 Obj *sym = intern(name,0);
 Obj *prim = make_primitive(fn,0);
 add_variable(env, sym, prim,0);
}

static void define_constants(Obj *env) {
 Obj *sym = intern("t",0);
 add_variable(env, sym, True,0);
}

static void define_grub(Obj * env, const char*s,int v, int ningle) {
 char toke[100];
 for (int i=0; i<ningle; i++){
  strcpy(toke,s);
  char c = 'a'+i;
  strncat(toke,&c,(i>0?1:0));
  //printf("%s\n",toke);
  add_variable(env,intern(toke,0),make_int(v+i,0),0);\
 }
}

 #define MEXPTOKE(s,f,ff,v,ningle) \
  define_grub(env,s,v,ningle);   
 #define JEXPTOKE(s,f,ff,v,ningle) \
  MEXPTOKE(s,f,ff,v,ningle)


 
static void define_primitives(Obj *env) {
	srand(time(0));
	#include "tokes.c"
    add_primitive(env, "fish", prim_fish);
    add_primitive(env, "soup", prim_soup);
    add_primitive(env, "tank", prim_tank);
    add_primitive(env, "boat", prim_boat);
    add_primitive(env, "\"", prim_car);
    add_primitive(env, "@\"", prim_setcar);

    add_primitive(env, ":", prim_cdr);
    add_primitive(env, "@:", prim_setcdr);
    add_primitive(env, "+", prim_add);
    add_primitive(env, "-", prim_minus);
    add_primitive(env, "/", prim_divide);
    add_primitive(env, "*", prim_mul);
    add_primitive(env, "&", prim_and);
    add_primitive(env, "|", prim_orr);
    add_primitive(env, "^", prim_xor);
    add_primitive(env, "!", prim_not);
    add_primitive(env, "%", prim_mod);
    add_primitive(env, "~", prim_rand);
    add_primitive(env, "@", prim_def);
        //add_primitive(env, "set", prim_set);
    add_primitive(env, "#", prim_fun);
    add_primitive(env, "?", prim_if);
    add_primitive(env, "=", prim_eq);
    add_primitive(env, ",", prim_lt);
    add_primitive(env, "'", prim_gt);

    add_primitive(env, "''", prim_shill);
    add_primitive(env, ",,", prim_shirr);
    add_primitive(env, "$", prim_print);
    add_primitive(env, "`", prim_euro);
    add_primitive(env, "exit", prim_exit);
}


void printar() {
#undef MEXPTOKE
#define MEXPTOKE(s,f,t,v,ningle) \
printf("%2x: %s %s: %s; %d pieces\n", v, s, f,t,ningle);
#undef JEXPTOKE
#define JEXPTOKE(s,f,ff,v,ningle) MEXPTOKE(s,f,ff,v,ningle)

  #include "tokes.c"
}
int simian;
void printarsimp() {
 simian = 8810202;
#undef MEXPTOKE
#define MEXPTOKE(s,f,t,v,ningle) \
if (simian!=v) \
 printf("%2x: %s %s: %s; %d pieces\n", v, s, f,t,ningle); //simian = v;
#undef JEXPTOKE
#define JEXPTOKE(s,f,ff,v,ningle) MEXPTOKE(s,f,ff,v,ningle)

  #include "tokes.c"
  
}



void pooler(Obj*env) {
 for (;looper;) {    
  Obj *expr = readhui();
  if (!expr) continue;
  if (expr == Cparen)
   error("Stray close parenthesis");
  if (expr == Dot)
   error("Stray dot");
  print(env,eval(env, expr,0));
  printf("\n");
 }  
}


int main(int argc, char * argv[]) {
 Dot = make_special(TDOT,0);
 Cparen = make_special(TCPAREN,0);
 True = make_special(TTRUE,0);
 Symbols = 0;
 Obj *env = make_env(0, 0,0);
 define_constants(env);
 define_primitives(env);
 char * inputfile=0;
 chubOPEN();
 int offset=1;
 if ((argc > offset) && (argv[1][0] == '-')) {
	 if (argv[1][1] == 'h') {

		 printarsimp();return 0;}
    if (argv[1][1] == 'g') {
         chubRUN();return 0;}
    if (argv[1][1] == 'o') {
         chubONE();return 0;}
	 if (argv[1][1] == 'z') {
		 printar();return 0;}
	 if (argv[1][1] == 'b') {
		 inputfile = strdup(argv[1]+2);
         offset=2;
     }
 } 
 //printf("here\n");
 if (argc > offset) {
  char * texte = strdup(argv[offset]);
  printf("opening %s\n",texte);
  freopen(texte,"r",stdin);
 }
 pooler(env); 
 situsb();
  
 if (inputfile) {
  FILE * filefish;
  filefish = fopen(inputfile,"rb");
  if (filefish) {
   while (masterbytesz < 0x10000)
    write_bytez(0);
   int c;
   if (0 == fseek (filefish , 0x20000 , SEEK_SET)) {
    c = fgetc (filefish);
    while (c != EOF) {  
     //if (masterbytesz < 195600)
     write_bytez((unsigned char)c);
     c = fgetc (filefish); 
    } 
   }
  //fclose(filefish);
  }
 }  
 int j;
 for (j = 0; j < 16; j++)
 write_bytez(0);
 chubSENDEND();//bytebuff,1);
 printf("\n");
}
