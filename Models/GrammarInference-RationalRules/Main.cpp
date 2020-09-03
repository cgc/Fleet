

// TODO: Might be easier to have responses be to the entire list of objects?

#include <assert.h>
#include <set>
#include <string>
#include <tuple>
#include <regex>
#include <vector>
#include <tuple>
#include <utility>
#include <functional>
#include <cmath>

#include "EigenLib.h"

#include "Object.h"

using S=std::string;

const double alpha = 0.9; // fixed for the learning part of the model

enum class Shape { rectangle, triangle, circle};
enum class Color { yellow, green, blue};
enum class Size  { size1, size2, size3};

// Define a kind of object with these features
// this is just a Fleet::Object, but with string constructors for simplicity
struct MyObject : public Object<Shape,Color,Size>  {
	
	MyObject() {}
	
	MyObject(S _shape, S _color, S _size) {
		// NOT: we could use magic_enum, but haven't here to avoid a dependency
		if     (_shape == "triangle")   this->set(Shape::triangle);
		else if(_shape == "rectangle")  this->set(Shape::rectangle);
		else if(_shape == "circle")     this->set(Shape::circle);
		else assert(0);
		
		if     (_color == "blue")     this->set(Color::blue);
		else if(_color == "yellow")   this->set(Color::yellow);
		else if(_color == "green")    this->set(Color::green);
		else assert(0);
		
		if     (_size == "1")        this->set(Size::size1);
		else if(_size == "2")        this->set(Size::size2);
		else if(_size == "3")        this->set(Size::size3);
		else assert(0);
	}
	
};

// An input here is a set of objects and responses
typedef std::pair<std::vector<MyObject>, MyObject> MyInput;
typedef bool                                       MyOutput;

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Primitives.h"
#include "Builtins.h"

const double FEATURE_WEIGHT = 5.0; // in our prior, what's the weight on features?

std::tuple PRIMITIVES = {
	Primitive("and(%s,%s)",     +[](bool a, bool b) -> bool { return (a and b); }), // optional specification of prior weight (default=1.0)
	Primitive("or(%s,%s)",      +[](bool a, bool b) -> bool { return (a or b); }),
	Primitive("not(%s)",        +[](bool a)         -> bool { return (not a); }),
	Primitive("iff(%s,%s)",     +[](bool a, bool b) -> bool { return (a == b); }),
	Primitive("implies(%s,%s)", +[](bool a, bool b) -> bool { return ((not a) or (a and b)); }),
	Primitive("xor(%s,%s)",     +[](bool a, bool b) -> bool { return (a xor b); }),
	Primitive("nand(%s,%s)",    +[](bool a, bool b) -> bool { return not (a and b); }),
	Primitive("nor(%s,%s)",     +[](bool a, bool b) -> bool { return not (a or b); }),
	// that + is really insane, but is needed to convert a lambda to a function pointer

	Primitive("yellow(%s)",    +[](MyObject x)       -> bool { return x.is(Color::yellow); }, FEATURE_WEIGHT),
	Primitive("green(%s)",     +[](MyObject x)       -> bool { return x.is(Color::green); }, FEATURE_WEIGHT),
	Primitive("blue(%s)",      +[](MyObject x)       -> bool { return x.is(Color::blue); }, FEATURE_WEIGHT),

	Primitive("rectangle(%s)", +[](MyObject x)       -> bool { return x.is(Shape::rectangle); }, FEATURE_WEIGHT),
	Primitive("triangle(%s)",  +[](MyObject x)       -> bool { return x.is(Shape::triangle); }, FEATURE_WEIGHT),
	Primitive("circle(%s)",    +[](MyObject x)       -> bool { return x.is(Shape::circle); }, FEATURE_WEIGHT),
	
	Primitive("size1(%s)",     +[](MyObject x)       -> bool { return x.is(Size::size1); }, FEATURE_WEIGHT),
	Primitive("size2(%s)",     +[](MyObject x)       -> bool { return x.is(Size::size2); }, FEATURE_WEIGHT),
	Primitive("size3(%s)",     +[](MyObject x)       -> bool { return x.is(Size::size3); }, FEATURE_WEIGHT),
	
	Primitive("%s.arg",         +[](MyInput x)       -> MyObject { return x.second; }),
		
	// but we also have to add a rule for the BuiltinOp that access x, our argument
	Builtin::X<MyInput>("x", 10.0)
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "Grammar.h"

class MyGrammar : public Grammar<bool,MyObject,MyInput> {
	using Super=Grammar<bool,MyObject,MyInput>;
	using Super::Super;
};

///~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Includes critical files. Also defines some variables (mcts_steps, explore, etc.) that get processed from argv 

#include "GrammarHypothesis.h" 
#include "LOTHypothesis.h"
#include "Data.h"


/// The type here is a little complex -- a LOTHypothesis here is type MyObject->bool even though
//  but the datum format we use is these sets, MyInput, MyOutput
class MyHypothesis final : public LOTHypothesis<MyHypothesis,MyInput,MyOutput,MyGrammar> {
public:
	// This is going to assume that all variables other than x are universally quantified over. 
	using Super = LOTHypothesis<MyHypothesis,MyInput,MyOutput,MyGrammar>;
	using Super::Super;
	using output_t = MyOutput; // needed since inheritance doesn't get these?
	
	double compute_single_likelihood(const datum_t& di) override {
		bool o = callOne(di.input, false);
		return log( (1.0-di.reliability)/2.0 + (o == di.output)*di.reliability);
	}
};

size_t grammar_callback_count = 0;
void gcallback(GrammarHypothesis<MyHypothesis>& h) {
	if(++grammar_callback_count % 100 == 0) {
		COUT h.string(str(grammar_callback_count)+"\t");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////
#include "Top.h"
#include "Fleet.h"
#include "Miscellaneous.h"
#include "MCMCChain.h"

S hypothesis_path = "hypotheses.txt";
S runtype         = "both"; // can be both, hypotheses (just find hypotheses), or grammar (just do mcmc, loading from hypothesis_path)

int main(int argc, char** argv){ 	
	Fleet fleet("An example of grammar inference for boolean concepts");
	fleet.add_option("--hypotheses",  hypothesis_path, "Where to load or save hypotheses.");
	fleet.add_option("--runtype",  runtype, "hypotheses = do we just do mcmc on hypotheses; grammar = mcmc on the grammar, loading the hypotheses; both = do both");
	fleet.initialize(argc, argv);
	
	MyGrammar grammar(PRIMITIVES);
	
	//------------------
	// set up the data
	//------------------
	
	std::vector<HumanDatum<MyHypothesis>> human_data;
	
	// We need to read the data and collapse same setNumbers and concepts to the same "set", since this is the new
	// data format for the model. 
	std::ifstream infile("preprocessing/data.txt");
	
	MyHypothesis::data_t* learner_data = nullptr; // pointer to a vector of learner data
	
	// what data do I run mcmc on? not just human_data since that will have many reps
	// NOTE here we store only the pointers to sequences of data, and the loop below loops
	// over items. This lets us preserve good hypotheses across amounts of data
	// but may not be as great at using parallel cores. 
	std::vector<MyHypothesis::data_t*> mcmc_data; 
	
	size_t ndata = 0;
	int    decay_position = 0;
	S prev_conceptlist = ""; // what was the previous concept/list we saw? 
	size_t LEANER_RESERVE_SIZE = 512; // reserve this much so our pointers don't break;
	
	while(! infile.eof() ) {
		if(CTRL_C) break;
		
		// use the helper above to read the next chunk of human data 
		// (and put the file pointer back to the start of the next chunk)
		auto [objs, corrects, yeses, nos, conceptlist] = get_next_human_data(infile);
		// could assert that the sizes of these are all the same
		
		// if we are on a new conceptlist, then make a new learner data (on stack)
		if(conceptlist != prev_conceptlist) {
			if(learner_data != nullptr) 
				mcmc_data.push_back(learner_data);
			
			// need to reserve enough here so that we don't have to move -- or else the pointers break
			learner_data = new MyHypothesis::data_t();
			learner_data->reserve(LEANER_RESERVE_SIZE);		
			ndata = 0;
			decay_position = 0;
		}
		
		// now put the relevant stuff onto learner_data
		for(size_t i=0;i<objs->size();i++) {
			MyInput inp{*objs, (*objs)[i]};
			learner_data->emplace_back(inp, (*corrects)[i], alpha);
			assert(learner_data->size() < LEANER_RESERVE_SIZE);
		}
				





		// TODO: CHECK IF THIS IS RIGHT WE SHOULD HAVE AN EMPTY element to start


		
				
		// now unpack this data into human_data for each point in the concept
		for(size_t i=0;i<objs->size();i++) {
			// make a map of the responses
			std::map<bool,size_t> m; m[true] = (*yeses)[i]; m[false] = (*nos)[i];
			
			HumanDatum<MyHypothesis> hd{learner_data, ndata, &((*learner_data)[ndata+i].input), std::move(m), 0.5, decay_position};
		
			human_data.push_back(std::move(hd));
		}
		
		ndata += objs->size();
		decay_position++;
		prev_conceptlist = conceptlist;
		
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//
		//		
		//if(human_data.size() > 100) break;
	
	}
	if(learner_data != nullptr) 
		mcmc_data.push_back(learner_data); // and add that last dataset
	
	COUT "# Loaded " << human_data.size() " human data points and " << mcmc_data.size() << " mcmc data points" ENDL;
	
	std::vector<MyHypothesis> hypotheses; 
	if(runtype == "hypotheses" or runtype == "both") {
		auto h0 = MyHypothesis::make(&grammar); 
		hypotheses = get_hypotheses_from_mcmc(h0, mcmc_data, Control(inner_mcmc_steps, inner_runtime), ntop);
		CTRL_C = 0; // reset control-C so we can break again for the next mcmc
		
		save(hypothesis_path, hypotheses);
	}
	else {
		// only load if we haven't run 
		hypotheses = load<MyHypothesis>(hypothesis_path, &grammar);
	}
	COUT "# Hypothesis size: " TAB hypotheses.size() ENDL;
	assert(hypotheses.size() > 0 && "*** Somehow we don't have any hypotheses!");
	
	if(runtype == "grammar" or runtype == "both") { 
		
		auto h0 = GrammarHypothesis<MyHypothesis>::make(hypotheses, &human_data);
	
		tic();
		auto thechain = MCMCChain(h0, &human_data, &gcallback);
		thechain.run(Control(mcmc_steps, runtime));
		tic();
	}
	
}