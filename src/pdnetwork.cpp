/***************************************************************************
 *   Copyright (C) 2006 by BUI Quang Minh, Steffen Klaere, Arndt von Haeseler   *
 *   minh.bui@univie.ac.at   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "mtree.h"
#include "pdnetwork.h"
#include "ncl/ncl.h"
#include "msetsblock.h"
#include "myreader.h" 
#include "lpwrapper.h"

PDNetwork::PDNetwork()
 : SplitGraph()
{
	extra_pd = 0;
	min_pd = false;
}

PDNetwork::PDNetwork(Params &params) : SplitGraph(params) {
	extra_pd = 0;
	min_pd = false;

	if (params.is_rooted) 
		readRootNode(ROOT_NAME);

	// read the parameter file
	if (params.param_file != NULL) 
		readParams(params);

	if (params.budget_file != NULL)
		if (isPDArea())
			pda->readBudgetAreaFile(params);
		else
			pda->readBudgetFile(params);

	// identify the root
	if (params.root != NULL) 
		readRootNode(params.root);

	// initial PD min
	if (params.find_pd_min)
		initPDMin();

	// read the initial set of taxa, incoporate info into the split system
	if (params.initial_file != NULL)
		readInitialSet(params);

	if (!initialset.empty() && !isPDArea())
		proceedInitialSet();

	if (params.initial_area_file != NULL)
		readInitialAreas(params);



}


/**
	Identify the root node if specified, include it into the initial set
	@param root_name name of the root node
*/
void PDNetwork::readRootNode(const char *root_name) {
	int id = -1;
	try {
		id = taxa->FindTaxon(root_name);
	} catch (NxsTaxaBlock::NxsX_NoSuchTaxon) {
		outError(ERR_NO_TAXON, root_name);
	}
	initialset.clear();
	initialset.push_back(id);
	//if (sets->getNSets() == 0)
}



void PDNetwork::readParams(Params &params) {
	int ntaxa = getNTaxa() - params.is_rooted;

	// read parameters from file	
	double scale;
	StrVector tax_name;
	DoubleVector ori_weight, tax_weight;
	readWeightFile(params, ntaxa, scale, tax_name, ori_weight);

	// now convert the weights
	tax_weight.resize(ntaxa, 0);
	for (int i = 0; i < tax_name.size(); i++) {	
		int id = -1;
		try {
			NxsString name = "";
			name.append(tax_name[i]);
			id = taxa->FindTaxon(name);
		} catch (NxsTaxaBlock::NxsX_NoSuchTaxon) {
			outError(ERR_NO_TAXON, tax_name[i]);
		}
		tax_weight[id] = ori_weight[i];
	}

	// incoporate into the split system
	for (iterator it = begin(); it != end(); it++) {
		int id = (*it)->trivial();
		// first, multiply split weight with the coefficient
		(*it)->weight *= scale;		

		// if a trivial split, add the important parameter f
		if (id >= 0)
			(*it)->weight += tax_weight[id];	
	}	
}


/**
	read the initial set of taxa to be included into PD-tree
	@param params program parameters
*/
void PDNetwork::readInitialSet(Params &params) {
	extra_pd = 0.0;
	int ntaxa = getNTaxa() - params.is_rooted;
	StrVector tax_name;
	readInitTaxaFile(params, ntaxa, tax_name);
	if (tax_name.empty()) 
		outError("No taxa found");
	for (StrVector::iterator it = tax_name.begin(); it != tax_name.end(); it++) {
		int id = -1;
		try {
			NxsString name = "";
			name.append(*it);
			id = taxa->FindTaxon(name);
		} catch (NxsTaxaBlock::NxsX_NoSuchTaxon) {
			outError(ERR_NO_TAXON, *it);
		}
		initialset.push_back(id);
	}

	if (isPDArea()) return;

	if (isBudgetConstraint()) {
		int budget = (params.budget >= 0) ? params.budget : pda->budget;
		if (calcCost(initialset) > budget)
			outError(ERR_TOO_SMALL_BUDGET);
	} else {
		int sub_size = (params.sub_size > 1) ? params.sub_size : pda->sub_size;
		if (initialset.size() > sub_size) 
			outError(ERR_TOO_SMALL_K);
	}
}

void PDNetwork::proceedInitialSet() {
	double total_w = trunc(abs(calcWeight())+1);
	// get the set of initial taxa
	set<int> iset;
	for (IntVector::iterator it2 = initialset.begin(); it2 != initialset.end(); it2++)
		iset.insert(*it2);
	// now modifying the split weights
	for (iterator it = begin(); it != end(); it++) {

		// get the taxa id of trivial split
		int id = (*it)->trivial();
		// if not trivial split, continue
		if (id < 0) continue;

		if (iset.find(id) != iset.end()) {
			// increase the trivial split weight
			(*it)->weight += total_w;
			extra_pd += total_w;			
		}
	}
}

void PDNetwork::readInitialAreas(Params &params) {
	if (!isPDArea())
		outError("Invalid -ia option: no areas specified");
	int nareas = sets->getNSets();
	StrVector area_name;
	readInitAreaFile(params, nareas, area_name);
	if (area_name.empty()) 
		outError("No area found");
	for (StrVector::iterator it = area_name.begin(); it != area_name.end(); it++) {
		int id = -1;
		id = sets->findArea(*it);
		if (id < 0)
			outError(ERR_NO_AREA, *it);
		initialareas.push_back(id);
	}

	if (isBudgetConstraint()) {
		int budget = (params.budget >= 0) ? params.budget : pda->budget;
		if (calcCost(initialareas) > budget)
			outError(ERR_TOO_SMALL_BUDGET);
	} else {
		int sub_size = (params.sub_size >= 1) ? params.sub_size : pda->sub_size;
		if (initialareas.size() > sub_size) 
			outError(ERR_TOO_SMALL_K);
	}
}


void PDNetwork::initPDMin() {
	min_pd = true;
	for (iterator it = begin(); it != end(); it++) {
		(*it)->weight = -(*it)->weight;
	}
}


/**
	compute the required costs to conserve a taxa set
	@param taxset set of taxa
	@return minimum budget required
*/
int PDNetwork::calcCost(IntVector &taxset) {
	int sum = 0;
	for (IntVector::iterator it = taxset.begin(); it != taxset.end(); it++)
		sum += pda->costs[*it];
	return sum;
}

/**
	compute the required costs to conserve a taxa set
	@param taxset set of taxa
	@return minimum budget required
*/
int PDNetwork::calcCost(Split &taxset) {
	IntVector invec;
	taxset.getTaxaList(invec);
	return calcCost(invec);
}



/********************************************************
	Now comes PD related stuff
********************************************************/



void PDNetwork::calcPD(Split &id_set) {
	if (initialset.empty()) {
		id_set.weight = calcWeight(id_set);
		return;
	}
	Split id(id_set);
	for (IntVector::iterator it = initialset.begin(); it != initialset.end(); it++)
		id.addTaxon(*it);
	id_set.weight = calcWeight(id);
}

void PDNetwork::calcExclusivePD(Split &id_set) {
	id_set.invert();
	calcPD(id_set);
	id_set.invert();
	id_set.weight = calcWeight() - id_set.weight;
}



void PDNetwork::computePD(Params &params, SplitSet &pd_set, PDRelatedMeasures &pd_more) {
	//MSetsBlock *sets;
	//sets = new MSetsBlock();

	//sets->Report(cout);
	TaxaSetNameVector *allsets = &sets->getSets();
	TaxaSetNameVector::iterator i;
	for (i = allsets->begin(); i != allsets->end(); i++) {
		Split *id_set = new Split(getNTaxa());
		/*
		for (IntVector::iterator it = initialset.begin(); it != initialset.end(); it++)
			id_set->addTaxon(*it);
		*/
		for (vector<NxsString>::iterator it2 = (*i).taxlist.begin(); it2 != (*i).taxlist.end(); it2++) {
			int id = -1;
			try {
				id = taxa->FindTaxon(*it2);
			} catch (NxsTaxaBlock::NxsX_NoSuchTaxon) {
				outError(ERR_NO_TAXON, *it2);
			}
			if (id >= 0)
				id_set->addTaxon(id);
		}
		pd_more.setName.push_back((*i).name);
		if (params.exclusive_pd) {
			calcExclusivePD(*id_set);
			pd_more.exclusivePD.push_back(id_set->getWeight());
		}
		calcPD(*id_set);
		pd_more.PDScore.push_back(id_set->weight);
		pd_set.push_back(id_set);
	}
	//delete sets;
}



/********************************************************
	EXHAUSTIVE FUNCTION
********************************************************/

void PDNetwork::updateSplitVector(Split &curset, SplitSet &best_set) 
{
	if (curset.weight > best_set[0]->weight) {
		for (int it = best_set.size()-1; it >= 0; it--) 
			delete best_set[it];
		best_set.clear();
	}
	best_set.push_back(new Split(curset));
}

/**
	calculate sum of weights of preserved splits in the taxa_set
	@param taxa_set a set of taxa
*/
double PDNetwork::calcRaisedWeight(Split &taxa_set, 
	IntList &rem_splits, IntList::iterator &rem_it)
{
	double sum = 0.0;
	for (IntList::iterator it = rem_splits.begin(); it != rem_it;)
		if ((*this)[*it]->preserved(taxa_set)) {
			sum += (*this)[*it]->weight;
			IntList::iterator prev_it = rem_it;
			prev_it--;
			int temp = *it;
			*it = *prev_it;
			*prev_it = temp;
			rem_it = prev_it;
		} else it++;
	return sum;
}

int PDNetwork::calcMaxBudget() {
	int sum = 0;
	for (IntVector::iterator it = pda->costs.begin(); it != pda->costs.end(); it++)
		sum += (*it);
	return sum;
}


void PDNetwork::enterFindPD(Params &params) {
	// check parameters
	if (isBudgetConstraint()) {
		int budget = (params.budget >= 0) ? params.budget : pda->getBudget();
		if (budget < 0) {
			outError(ERR_NO_BUDGET);
		}
	} else {
		int min_accepted = !isPDArea() + 1;
		int sub_size = (params.sub_size >= min_accepted) ? params.sub_size : pda->getSubSize();
		if (sub_size < min_accepted && params.pdtaxa_file == NULL) {
			outError(ERR_NO_K);
		}
		
	}

	if (initialset.size() > 0) {
		cout << "Consider split network as ROOTED." << endl;
	} else {
		cout << "Consider split network as UNROOTED." << endl;
	}

	cout << "Total split weights: " << calcWeight() << endl;
	cout << "  Internal split weights: " << calcWeight() - calcTrivialWeight() << endl;
	cout << "  Trivial split weights : " << calcTrivialWeight() << endl;

	if (isBudgetConstraint()) {
		// fix the budget and min_budget first
		if (params.budget < 0) params.budget = pda->budget;
		if (verbose_mode >= VB_MED) {
			pda->Report(cout);
		}
		cout << "Budget constraint with budget = " << params.budget << " ..." << endl;
		if (params.min_budget < 0)
			params.min_budget = pda->min_budget;
		if (params.min_budget < 0) params.min_budget = params.budget;

		// resize the taxa_set
		int max_budget = calcMaxBudget();
		if (params.budget > max_budget) {
			cout << "Only maximum budget of " << max_budget << " required, truncating to that value..." << endl;
			params.budget = max_budget;
			if (params.min_budget > params.budget)
				params.min_budget = params.budget;
		}
	
	} else	{
		int min_accepted = !isPDArea() + 1;
		if (params.sub_size <= 0) params.sub_size = pda->sub_size;
		if (!isPDArea()) {
			if (params.sub_size < 2 || params.sub_size > getNTaxa()) {
				NxsString str = "k must be between 2 and ";
				str += getNTaxa()-params.is_rooted;
				outError(str);
			}
		} else if (params.sub_size < 1 || params.sub_size > sets->getNSets()) {
				NxsString str = "k must be between 1 and ";
				str += sets->getNSets();
				outError(str);
			}
		if (params.min_size < min_accepted) params.min_size = params.sub_size;
	}
	
}

void PDNetwork::findPD(Params &params, vector<SplitSet> &taxa_set, vector<int> &taxa_order) {

	// call the entering function
	enterFindPD(params);

	int ntaxa = getNTaxa();
	int nsplits = getNSplits();
	Split curset(ntaxa, 0.0);
	IntList rem_splits;

	for (int i = 0; i < nsplits; i++) 
		rem_splits.push_back(i);
	IntList::iterator rem_it = rem_splits.end();

	params.detected_mode = EXHAUSTIVE;

	if (isPDArea()) {
		params.detected_mode = LINEAR_PROGRAMMING;
		cout << "Optimizing PD over " << sets->getNSets() << " areas..." << endl;
		cout << "Using LP_SOLVE" << endl;
		cout << "Linear programming on general split network..." << endl;
		findPDArea_LP(params, taxa_set);
	} else if (params.run_mode == GREEDY) {	
		// greedy search, not ensure to give the optimal sets!
		cout << "Start greedy search..." << endl;
		greedyPD(params.sub_size, curset, taxa_order);
		localSearchPD(params.sub_size, curset, taxa_order);
		taxa_set.resize(1);
		taxa_set[0].push_back(new Split(curset));
	} else if (params.run_mode != EXHAUSTIVE) {
		params.detected_mode = LINEAR_PROGRAMMING;
		cout << "Using LP_SOLVE" << endl;
		cout << "Linear programming on general split network..." << endl;
		findPD_LP(params, taxa_set);
	} 
	else if (isBudgetConstraint()) {
		// exhaustive search by the order
		cout << endl << "Start exhaustive search..." << endl;
		taxa_set.resize(1);
		taxa_set[0].push_back(new Split(ntaxa, 0.0));
		exhaustPDBudget(params.budget, -1, curset, params.find_all, taxa_set[0], taxa_order, rem_splits, rem_it);
	} else	{
		// exhaustive search by the order
		cout << endl << "Start exhaustive search..." << endl;
		taxa_set.resize(1);
		taxa_set[0].push_back(new Split(ntaxa, 0.0));
		exhaustPD2(params.sub_size, -1, curset, params.find_all, taxa_set[0], taxa_order, rem_splits, rem_it);
	}

	// call the leaving function
	leaveFindPD(taxa_set);
}

void PDNetwork::leaveFindPD(vector<SplitSet> &taxa_set) {
	// subtract the weights from the extra_pd
	if (extra_pd > 0)
		for (vector<SplitSet>::iterator it = taxa_set.begin(); it != taxa_set.end(); it++) 
			for (SplitSet::iterator it2 = (*it).begin(); it2 != (*it).end(); it2++)
				(*it2)->weight -= extra_pd;
	if (min_pd) 
		for (vector<SplitSet>::iterator it = taxa_set.begin(); it != taxa_set.end(); it++) 
			for (SplitSet::iterator it2 = (*it).begin(); it2 != (*it).end(); it2++)
				(*it2)->weight = -(*it2)->weight;
}


/**
	exhaustive search VERSION 2 for maximal phylogenetic diversity of a given size 
	@param subsize the subset size
	@param best_set (OUT) the set of taxa in the maximal PD set
	@param cur_tax current taxon
	@param curset current set
	@param taxa_order (OUT) order of inserted taxa
	@param rem_splits (IN) remaining splits
	@return the PD score of the maximal set
*/
double PDNetwork::exhaustPD2(int subsize, int cur_tax, Split &curset, 
	bool find_all,SplitSet &best_set, vector<int> &taxa_order, 
	IntList &rem_splits, IntList::iterator &rem_it ) 
{
	int ntaxa = getNTaxa();
	double saved_score = curset.weight;
	for (int tax = cur_tax+1; tax <= ntaxa - subsize; tax ++) {
		curset.addTaxon(taxa_order[tax]);
		IntList::iterator saved_it = rem_it;
		curset.weight += calcRaisedWeight(curset, rem_splits, rem_it);
		if (subsize > 1)
			exhaustPD2(subsize-1, tax, curset, find_all, best_set, taxa_order, rem_splits, rem_it);
		else {
			if (curset.weight >= best_set[0]->weight) {
				updateSplitVector(curset, best_set);
				//curset.report(cout);
			}
			//curset.report(cout);
		}
		curset.removeTaxon(taxa_order[tax]);
		curset.weight = saved_score;
		rem_it = saved_it;
		//restoreSplit(subsize, rem_splits, out_splits);
	}
	return best_set[0]->weight;
}



double PDNetwork::exhaustPDBudget(int cur_budget, int cur_tax, Split &curset, 
	bool find_all,SplitSet &best_set, vector<int> &taxa_order, 
	IntList &rem_splits, IntList::iterator &rem_it ) 
{
	int ntaxa = getNTaxa();
	double saved_score = curset.weight;
	for (int tax = cur_tax+1; tax < ntaxa; tax ++) 
	if (pda->costs[taxa_order[tax]] <= cur_budget)
	{
		curset.addTaxon(taxa_order[tax]);
		IntList::iterator saved_it = rem_it;
		
		curset.weight += calcRaisedWeight(curset, rem_splits, rem_it);
		if (curset.weight >= best_set[0]->weight) {
			updateSplitVector(curset, best_set);
			//curset.report(cout);
		}

		if (tax < ntaxa-1)
			exhaustPDBudget(cur_budget - pda->costs[taxa_order[tax]], tax, 
				curset, find_all, best_set, taxa_order, rem_splits, rem_it);
		
			//curset.report(cout);
		curset.removeTaxon(taxa_order[tax]);
		curset.weight = saved_score;
		rem_it = saved_it;
		//restoreSplit(subsize, rem_splits, out_splits);
	}
	return best_set[0]->weight;
}


/********************************************************
	GREEDY SEARCH!
********************************************************/

/**
	greedy algorithm for phylogenetic diversity of a given size 
	@param subsize the subset size
	@param taxa_set (OUT) the set of taxa in the PD-set
	@return the PD score of the maximal set, also returned in taxa_set.weight
*/
double PDNetwork::greedyPD(int subsize, Split &taxa_set, vector<int> &taxa_order) {
	int ntaxa = getNTaxa();
	taxa_set.setNTaxa(ntaxa);
	taxa_set.weight = 0;
	taxa_order.clear();
	taxa_order.reserve(ntaxa);

	int besti, bestj, i, j;

	// start from the PD-2 set
	for (i = 0; i < ntaxa - 1; i++)
		for (j = 0; j < ntaxa; j++) {
			Split curset;
			curset.setNTaxa(ntaxa);
			curset.addTaxon(i);
			curset.addTaxon(j);
			curset.weight = calcWeight(curset);
			if (curset.weight > taxa_set.weight) {
				taxa_set = curset;
				besti = i;
				bestj = j;
			}
		}

	//taxa_set.report(cout);
	taxa_order.push_back(besti);
	taxa_order.push_back(bestj);

	for (int step = 2; step < subsize; step++) {
		Split pdk_set = taxa_set;
		besti = -1;
		for (i = 0; i < ntaxa; i++) 
		if (!pdk_set.containTaxon(i)) {
			Split curset;
			curset.setNTaxa(ntaxa);
			curset = pdk_set;
			curset.addTaxon(i);
			curset.weight = calcWeight(curset);
			if (curset.weight > taxa_set.weight || besti == -1) {
				taxa_set = curset;
				besti = i;
			}
		}
		//taxa_set.report(cout);
		taxa_order.push_back(besti);
	}
	return taxa_set.getWeight();
}


/**
	testing algorithm for phylogenetic diversity of a given size 
	@param subsize the subset size
	@param taxa_set (OUT) the set of taxa in the PD-set
	@return the PD score of the maximal set, also returned in taxa_set.weight
*/
double PDNetwork::localSearchPD(int subsize, Split &taxa_set, vector<int> &taxa_order) {
	int ntaxa = getNTaxa();
	//int nsplits = getNSplits();
	int i;
	taxa_set.setNTaxa(ntaxa);
	for (i = 0; i < subsize; i++) 
		taxa_set.addTaxon(taxa_order[i]);
	taxa_set.weight = calcWeight(taxa_set);
	taxa_set.report(cout);
	bool stop;
	do {
		stop = true;
		for (i = 0; i < ntaxa; i++) if (taxa_set.containTaxon(i)) {
			for (int j = 0; j < ntaxa; j++) if (!taxa_set.containTaxon(j)) 
			{
				taxa_set.addTaxon(j);
				taxa_set.removeTaxon(i);
				double new_w = calcWeight(taxa_set);
				if (new_w > taxa_set.weight) {
					taxa_set.weight = new_w;
					stop = false;
					taxa_set.report(cout);
					break;
				}
				taxa_set.removeTaxon(j);
				taxa_set.addTaxon(i);
			}
			if (!stop) break;
		}
	} while (!stop);
	return taxa_set.getWeight();
}


void PDNetwork::calcPDGain(vector<SplitSet> &pd_set, matrix(double) &delta) {
	vector<SplitSet>::iterator it;
	int ntaxa = pd_set.front().front()->getNTaxa();
	delta.resize(pd_set.size());
	int cnt = 0;
	for (cnt = 0; cnt < delta.size(); cnt++) 
		delta[cnt].resize(ntaxa, 0);



	for (it = pd_set.begin(), cnt = 0; it != pd_set.end(); it++, cnt++) {
		assert(!(*it).empty());
		// take only the first split for calculation
		Split *sp = (*it).front();
		for (int tax = 0; tax < ntaxa; tax++)
			if (!sp->containTaxon(tax)) {
				sp->addTaxon(tax);
				delta[cnt][tax] = calcWeight(*sp) - sp->weight;
				sp->removeTaxon(tax);
			}
	}
}

void PDNetwork::calcPDEndemism(SplitSet &area_set, DoubleVector &pd_endem) {
	SplitSet::iterator it_s;

	// make union of all id_sets
	Split id_union(getNTaxa());
	for (it_s = area_set.begin(); it_s != area_set.end(); it_s++) 
		id_union += *(*it_s);
	
	// calculate PD of union 
	calcPD(id_union);

	// now calculate PD endemism
	pd_endem.clear();
	for (it_s = area_set.begin(); it_s != area_set.end(); it_s++) {
		// make union of all other set
		Split id_other(getNTaxa());
		for (SplitSet::iterator it_s2 = area_set.begin(); it_s2 != area_set.end(); it_s2++)
			if (it_s2 != it_s) id_other += *(*it_s2);
		// calculate PD of all other sets
		calcPD(id_other);

		// calc PD endemism
		pd_endem.push_back(id_union.weight - id_other.weight);
	}
}

void PDNetwork::calcPDComplementarity(SplitSet &area_set, char *area_names, 
	vector<NxsString> &all_names, DoubleVector &pd_comp) {

	set<string> given_areas;

	parseAreaName(area_names, given_areas);

/*
	for (set<string>::iterator it = given_areas.begin(); it != given_areas.end(); it++)
		cout << (*it) << "!";
	cout << endl;
*/
	SplitSet::iterator it_s;
	vector<NxsString>::iterator it_n;

	Split given_id(getNTaxa());

	// convert taxa set to id set
	for (it_s = area_set.begin(), it_n = all_names.begin(); it_s != area_set.end(); it_s++, it_n++) {
		if (given_areas.find(*it_n) != given_areas.end())
			given_id += *(*it_s);
	}
	
	if (given_id.countTaxa() == 0)
		outError("Complementary area name(s) not correct");

	calcPD(given_id);

	// now calculate PD complementarity
	pd_comp.clear();
	for (it_s = area_set.begin(); it_s != area_set.end(); it_s++) {
		// make union the two sets
		Split id_both(*(*it_s));
		id_both += given_id;
		// calculate PD of both sets
		calcPD(id_both);
		// calc PD complementarity
		pd_comp.push_back(id_both.weight - given_id.weight);
	}

}

void PDNetwork::checkYValue(int total_size, vector<int> &y_value) {
	iterator spit;
	int ntaxa = getNTaxa();
	int i;

	y_value.resize(getNSplits(), -1);
	for (spit = begin(),i=0; spit != end(); spit++,i++) {
		Split *sp = (*spit);
		int id = -1;
		int cnt = sp->countTaxa();
		if (cnt > ntaxa / 2) {
			sp->invert();
			cnt = ntaxa - cnt;
		}
		if (cnt == 1)
			id = sp->firstTaxon();
		if (id >= 0) {
			// if the split is external -> y[i] = x[id]
			y_value[i] = id+2;
			continue;
		}
		if (!isBudgetConstraint()) {
			if (cnt > ntaxa - total_size && cnt < total_size) {
				// if both constraints can be dropped -> y[i] = 1
				y_value[i] = 1;
			}
		}
	}
	
}



/**
	add binary variables
*/
void addBinLP2(const char *outfile, int nvars, Split &included_var) {
	try {
		ofstream out;
		out.exceptions(ios::failbit | ios::badbit);
		out.open(outfile, ios::app);
		bool first = true;
		for (int i = 0; i < nvars; i++) {
			if (included_var.containTaxon(i)) continue;
			if (first)
				out << "bin x" << i;
			else out << ", x" << i;
			first = false;
		}
		if (!first)
			out << ";" << endl;
		out.close();
	} catch (ios::failure) {
		outError(ERR_WRITE_OUTPUT, outfile);
	}
	
}

void addBinLP(const char *outfile, int nvars, IntVector &initialset) {
	Split included_var(nvars);
	for (IntVector::iterator it2 = initialset.begin(); it2 != initialset.end(); it2++)
		included_var.addTaxon(*it2);
	addBinLP2(outfile, nvars, included_var);
}

void PDNetwork::transformLP(Params &params, const char *outfile, int total_size, bool make_bin) {
	Split included_tax(getNTaxa());
	IntVector::iterator it2;
	for (it2 = initialset.begin(); it2 != initialset.end(); it2++)
		included_tax.addTaxon(*it2);

	try {
		ofstream out;
		out.exceptions(ios::failbit | ios::badbit);
		out.open(outfile);
		iterator spit;
		int i,j;
		int ntaxa = getNTaxa();
		vector<int> y_value;
		checkYValue(total_size, y_value);

		// define the objective function
		out << "max: ";
		
		for (spit = begin(),i=0; spit != end(); spit++,i++) {
			if (y_value[i] < 0)
				out << " +" << (*spit)->getWeight() << " y" << i;
			else if (y_value[i] >= 2) 
				out << " +" << (*spit)->getWeight() << " x" << y_value[i]-2;
		}
		out << ";" << endl;

		// adding the constraint for splits
		for (spit = begin(),i=0; spit != end(); spit++,i++) {
			if (y_value[i] >= 0) continue;
			
			Split *sp = (*spit);
			bool contain_initset = sp->containAny(initialset);

			if (!contain_initset && (isBudgetConstraint() || sp->countTaxa() <= ntaxa - total_size)) {
				out << "y" << i << " <=";
				for (j = 0; j < ntaxa; j++)
					if (sp->containTaxon(j))
						out << " +x" << j;
				out << ";" << endl;
			}
			contain_initset = false;
			if (initialset.size() > 0) {
				sp->invert();
				contain_initset =  sp->containAny(initialset);
				sp->invert();
			}
			if (!contain_initset && (isBudgetConstraint() || sp->countTaxa() >= total_size)) {
				out << "y" << i << " <=";
				if (!isBudgetConstraint())
					out << total_size;
				for (j = 0; j < ntaxa; j++) 
					if (isBudgetConstraint()) {
						if (!sp->containTaxon(j))
							out << " +x" << j;
					} else
						if (sp->containTaxon(j)) 
							out << " -x" << j;
				
				out << ";" << endl;
			}
		}

		// constraint for k-set or total budget
		if (isBudgetConstraint()) {
			out << total_size << " >=";
			for (j = 0; j < ntaxa; j++) {
				out << " +" << getPdaBlock()->getCost(j) << " x" << j;
			}
			out << ";" << endl;
		} else {
			out << total_size << " = ";
			for (j = 0; j < ntaxa; j++) {
				out << " +x" << j;
			}
			out << ";" << endl;
		}
		
		// define the variable boundary
		for (it2 = initialset.begin(); it2 != initialset.end(); it2++)
			out << "x" << *it2 << " = 1;" << endl;

		for (i = 0; i < getNSplits(); i++) {
			if (y_value[i] >= 0) continue;
			out << "y" << i << " <= 1;" << endl;
		}

		bool first = true;
		for (j = 0; j < ntaxa; j++) {
			if (included_tax.containTaxon(j)) continue;
			if (make_bin) {
				if (!first) 
					out << ", ";
				else
					out << "bin ";
				out << "x" << j;
			} else
				out << "x" << j << " <= 1;" << endl;
			first = false;
		}
		if (make_bin && !first)
			out << ";" << endl;
		out.close();
		//cout << "Transformed LP problem printed to " << outfile << endl;
	} catch (ios::failure) {
		outError(ERR_WRITE_OUTPUT, outfile);
	}
}

void PDNetwork::findPD_LP(Params &params, vector<SplitSet> &taxa_set) {
	if (params.find_all)
		outError("Current linear programming does not support multiple optimal sets!");

	string ofile = params.user_file;
	ofile += ".lpx";
	double score;
	int lp_ret, i, ntaxa = getNTaxa();
	int k, min_k, max_k, step_k, index;

	double *variables = new double[ntaxa];

	if (isBudgetConstraint()) { // non-budget case
		min_k = params.min_budget;
		max_k = params.budget;
		step_k = params.step_budget;
	} else {
		min_k = params.min_size;
		max_k = params.sub_size;
		step_k = params.step_size;
	}
	taxa_set.resize((max_k - min_k)/step_k + 1);

	// now construction the optimal PD sets
	if (isBudgetConstraint())
		cout << "running budget = ";
	else
		cout << "running k = ";
	for (k = min_k; k <= max_k; k += step_k) {
		index = (k - min_k) / step_k;
		if (!params.binary_programming) {
			transformLP(params, ofile.c_str(), k, false);
			cout << " " << k;
			cout.flush();
			lp_ret = lp_solve((char*)ofile.c_str(), ntaxa, &score, variables, verbose_mode);
		} else lp_ret = 7;
		if (lp_ret != 0 && lp_ret != 7)
			outError("Something went wrong with LP_SOLVE!");
		if (lp_ret == 7) { // fail with non-binary case, do again with strict binary
			if (params.binary_programming)
				transformLP(params, ofile.c_str(), k, true);
			else 
				addBinLP(ofile.c_str(), ntaxa, initialset);
			cout << " " << k << "(bin)";
			cout.flush();
			lp_ret = lp_solve((char*)ofile.c_str(), ntaxa, &score, variables, verbose_mode);
			if (lp_ret != 0) // check error again without allowing non-binary
				outError("Something went wrong with LP_SOLVE!");
		}	

		Split *pd_set = new Split(ntaxa, score);
		for (i = 0; i < ntaxa; i++)
			if (1.0 - variables[i] < tolerance) {
				//pd_set->addTaxon(taxa_order[i]);
				pd_set->addTaxon(i);
			}
		calcPD(*pd_set);
		taxa_set[index].push_back(pd_set);
	}
	cout << endl;
	delete variables;	
}

void PDNetwork::checkYValue_Area(int total_size, vector<int> &y_value, vector<int> &count1, vector<int> &count2) {
	iterator spit;
	int nareas = area_taxa.size();
	int i, j;

	y_value.resize(getNSplits(), -1);
	count1.resize(getNSplits(), 0);
	count2.resize(getNSplits(), 0);
	for (spit = begin(),i=0; spit != end(); spit++,i++) {
		Split *sp = (*spit);
		int id1 = -1, id2 = -1;
		for (j = 0; j < nareas; j++) {
			if (sp->overlap(*area_taxa[j])) { 
				count1[i]++;
				id1 = j;
			}
		}
		sp->invert();
		for (j = 0; j < nareas; j++) {
			if (sp->overlap(*area_taxa[j])) { count2[i]++; id2 = j; }
		}
		sp->invert(); // invert back to original
		if (isBudgetConstraint())
			continue;
		if (count1[i] == 0 || count2[i] == 0) 
			y_value[i] = 0;
		else if (count1[i] == 1 && count2[i] > nareas - total_size) {
			y_value[i] = id1 + 2;
		} else if (count2[i] == 1 && count1[i] > nareas - total_size) {
			y_value[i] = id2 + 2;
			//continue;
		} else if (count1[i] > nareas - total_size && count2[i] > nareas - total_size) {
			y_value[i] = 1;
		}
	}
	
}

void PDNetwork::transformLP_Area(Params &params, const char *outfile, int total_size, bool make_bin) {
	Split included_area(getNAreas());
	IntVector::iterator it2;
	for (it2 = initialareas.begin(); it2 != initialareas.end(); it2++)
		included_area.addTaxon(*it2);
	try {
		ofstream out;
		out.exceptions(ios::failbit | ios::badbit);
		out.open(outfile);
		iterator spit;
		int i,j;
		int nareas = area_taxa.size();
		vector<int> y_value, count1, count2;

		checkYValue_Area(total_size, y_value, count1, count2);

		// define the objective function
		out << "max: ";
		
		for (spit = begin(),i=0; spit != end(); spit++,i++)	{
			if (y_value[i] < 0)
				out << " +" << (*spit)->getWeight() << " y" << i;
			else if (y_value[i] >= 2)
				out << " +" << (*spit)->getWeight() << " x" << y_value[i] - 2;
		}
		out << ";" << endl;

		// adding the constraint for splits
		for (spit = begin(),i=0; spit != end(); spit++,i++) {
			if (y_value[i] >= 0) continue;
			Split *sp = (*spit);

			if (sp->countTaxa() == 0)
				cout << "0 taxa" << endl;
			if (isBudgetConstraint() || count1[i] <= nareas - total_size) 
			{
				out << "y" << i << " <=";
				for (j = 0; j < nareas; j++) {
					if (sp->overlap(*area_taxa[j]))
						out << " +x" << j;
				}
				out << ";" << endl;
			}

			if (isBudgetConstraint() || count2[i] <= nareas - total_size) 
			{
				sp->invert(); // scan the invert
				if (sp->countTaxa() == 0)
					cout << "0 taxa" << endl;
				out << "y" << i << " <=";
				for (j = 0; j < nareas; j++) {
					if (sp->overlap(*area_taxa[j]))
						out << " +x" << j;
				}
				out << ";" << endl;
				sp->invert(); // invert back to original
			}
		}
		// adding constraint for initialset
		for (IntVector::iterator it = initialset.begin(); it != initialset.end(); it++) {
			if (it == initialset.begin() && (params.root || params.is_rooted)) // ignore the root
				continue;
			out << "1 <= ";
			bool ok = false;
			for (j = 0; j < nareas; j++)
				if (area_taxa[j]->containTaxon(*it)) {
					out << " +x" << j;
					ok = true;
				}
			out << ";" << endl;
			if (!ok) {
				outError("No area contains taxon ", taxa->GetTaxonLabel(*it));
			}
		}

		// constraint for k-set or total budget
		if (isBudgetConstraint()) {
			out << total_size << " >=";
			for (j = 0; j < nareas; j++) {
				out << " +" << getPdaBlock()->getCost(j) << " x" << j;
			}
			out << ";" << endl;
		} else {
			out << total_size << " = ";
			for (j = 0; j < nareas; j++) {
				out << " +x" << j;
			}
			out << ";" << endl;
		}

		// define the variable boundary
		for (it2 = initialareas.begin(); it2 != initialareas.end(); it2++)
			out << "x" << *it2 << " = 1;" << endl;

		for (i = 0; i < getNSplits(); i++) {
			if (y_value[i] >= 0) continue;
			out << "y" << i << " <= 1;" << endl;
		}

		
		bool first = true;
		for (j = 0; j < nareas; j++) {
			if (included_area.containTaxon(j)) continue;
			if (make_bin) {
				if (!first) 
					out << ", ";
				else
					out << "bin ";
				out << "x" << j;
			} else
				out << "x" << j << " <= 1;" << endl;
			first = false;
		}
		if (make_bin && !first)
			out << ";" << endl;
		out.close();
		//cout << "Transformed LP problem printed to " << outfile << endl;
	} catch (ios::failure) {
		outError(ERR_WRITE_OUTPUT, outfile);
	}

}

void PDNetwork::findPDArea_LP(Params &params, vector<SplitSet> &areas_set) {
	if (params.find_all)
		outError("Current linear programming does not support multiple optimal sets!");
	PDRelatedMeasures pd_more;
	// get the taxa in the areas
	computePD(params, area_taxa, pd_more);
	if (params.root || params.is_rooted) {
		assert(!initialset.empty());
		int root_id = initialset[0];
		for (SplitSet::iterator it = area_taxa.begin(); it != area_taxa.end(); it++)
			(*it)->addTaxon(root_id);
	}
	
	int num_area_coverage = params.sub_size;
	Split area_coverage;
	if (checkAreaCoverage()) {
		num_area_coverage = findMinAreas(params, area_coverage);
		cout << "NOTE: it is determined that ";
		if (isBudgetConstraint())
			cout << "a budget of " << num_area_coverage << " is enough";
		else
			cout << "a number of " << num_area_coverage << " areas are enough";
		cout << " to cover all taxa" << endl;
		if (isBudgetConstraint()) {
			if (params.budget > num_area_coverage) {
				params.budget = num_area_coverage;
				if (params.min_budget > params.budget)
					params.min_budget = params.budget;
				cout << "budget is therefore set to a maximum of " << num_area_coverage << endl;
			}
		} else
		if (params.sub_size > num_area_coverage) {
			params.sub_size = num_area_coverage;
			if (params.min_size > params.sub_size) 
				params.min_size = params.sub_size;
			cout << "k is therefore set to a maximum of " << num_area_coverage << endl;
		}
	}

	string ofile = params.user_file;
	ofile += ".lpx";
	double score;
	int lp_ret, i;
	int nareas = area_taxa.size();
	int k, min_k, max_k, step_k, index;

	double *variables = new double[nareas];

	if (isBudgetConstraint()) { // non-budget case
		min_k = params.min_budget;
		max_k = params.budget;
		step_k = params.step_budget;
	} else {
		min_k = params.min_size;
		max_k = params.sub_size;
		step_k = params.step_size;
	}
	areas_set.resize((max_k - min_k)/step_k + 1);

	// now construction the optimal PD sets
	if (isBudgetConstraint())
		cout << "running budget = ";
	else
		cout << "running k = ";
	for (k = min_k; k <= max_k; k += step_k) {
		index = (k - min_k) / step_k;
		if (!params.binary_programming) {
			cout << " " << k;
			cout.flush();
			transformLP_Area(params, ofile.c_str(), k, false);
			lp_ret = lp_solve((char*)ofile.c_str(), nareas, &score, variables, verbose_mode);
		} else lp_ret = 7;
		if (lp_ret != 0 && lp_ret != 7)
			outError("Something went wrong with LP_SOLVE!");
		if (lp_ret == 7) { // fail with non-binary case, do again with strict binary
			cout << " " << k << "(bin)";
			cout.flush();
			if (params.binary_programming)
				transformLP_Area(params, ofile.c_str(), k, true);
			else
				addBinLP(ofile.c_str(), nareas, initialareas);
			lp_ret = lp_solve((char*)ofile.c_str(), nareas, &score, variables, verbose_mode);
			if (lp_ret != 0) // check error again without allowing non-binary
				outError("Something went wrong with LP_SOLVE!");
		}	

		Split *area = new Split(nareas, score);
		for (i = 0; i < nareas; i++)
			if (1.0 - variables[i] < tolerance) {
				//pd_set->addTaxon(taxa_order[i]);
				area->addTaxon(i);
			}
		calcPDArea(*area);
		areas_set[index].push_back(area);
	}
	cout << endl;
	delete variables;	
}


bool PDNetwork::isPDArea() {
	return (sets->getNSets() > 0);
}

void PDNetwork::calcPDArea(Split &area_id_set) {
	int ntaxa = getNTaxa();
	int nareas = area_taxa.size();
	Split sp(ntaxa);
	for (int i = 0; i < nareas; i++)
		if (area_id_set.containTaxon(i))
			sp += *area_taxa[i];
	calcPD(sp);
	area_id_set.weight = sp.weight;
}

bool PDNetwork::isUniquelyCovered(int taxon, int &area) {
	area = -1;
	for (int i = 0; i < getNAreas(); i++)
		if (area_taxa[i]->containTaxon(taxon)) {
			if (area < 0) area = i;	else return false;
		}
	return (area >= 0);
}


void PDNetwork::transformLP_Area_Coverage(const char *outfile, Split &included_area) {
	int ntaxa = getNTaxa();
	int nareas = getNAreas();
	int i, j;
	IntVector::iterator it;
	Split tax_cover(ntaxa);
	for (it = initialareas.begin(); it != initialareas.end(); it++) {
		tax_cover += *(area_taxa[*it]);
		included_area.addTaxon(*it);
	}
	for (j = 0; j < ntaxa; j++) {
		if (isUniquelyCovered(j, i)) {
			if (verbose_mode > VB_MIN) {
				cout << "Taxon " << taxa->GetTaxonLabel(j) << " is uniquely covered by " << sets->getSet(i).name << endl;
			}
			included_area.addTaxon(i);
			tax_cover.addTaxon(j);
		}
	}	
	try {
		ofstream out;
		out.exceptions(ios::failbit | ios::badbit);
		out.open(outfile);
		iterator spit;

		// define the objective function as minimum sum of x, x_i=1 if area i is chosen.
		out << "min:";
		for (i = 0; i < nareas; i++) {
			if (isBudgetConstraint())
				out << " +" << pda->getCost(i) << " x" << i; 
			else
				out << " +x" << i;
		}
		out << ";" << endl;

		// add constraint: every taxon should be covered by some area
		for (j = 0; j < ntaxa; j++) {
			if (tax_cover.containTaxon(j)) continue;
			out << "1 <=";
			for (i = 0; i < nareas; i++)
				if (area_taxa[i]->containTaxon(j))
					out << " +x" << i;
			out << ";" << endl;
		}
		// add bound for variable x
		for (i = 0; i < nareas; i++) {
			if (included_area.containTaxon(i)) 
				out << "x" << i << " = 1;" << endl;
			else
			out << "x" << i << " <= 1;" << endl;
		}
		out.close();
	} catch (ios::failure) {
		outError(ERR_WRITE_OUTPUT, outfile);
	}
}


int PDNetwork::findMinAreas(Params &params, Split &area_id) {
	string ofile = params.user_file;
	ofile += ".lpx";
	int nareas = getNAreas();
	int i;
	double *variables = new double[nareas];
	double score;
	Split included_area(nareas);
	transformLP_Area_Coverage(ofile.c_str(), included_area);
	int lp_ret = lp_solve((char*)ofile.c_str(), nareas, &score, variables, verbose_mode);

	if (lp_ret != 0 && lp_ret != 7)
		outError("Something went wrong with LP_SOLVE!");
	if (lp_ret == 7) { // fail with non-binary case, do again with strict binary
		addBinLP2(ofile.c_str(), nareas, included_area);
				
		lp_ret = lp_solve((char*)ofile.c_str(), nareas, &score, variables, verbose_mode);
		if (lp_ret != 0) // check error again without allowing non-binary
			outError("Something went wrong with LP_SOLVE!");
	}
	area_id.setNTaxa(nareas);
	int count = 0;
	// for checking purpose
	Split taxon_coverage(getNTaxa());

	for (i = 0; i < nareas; i++)
		if (1.0 - variables[i] < tolerance) {
			//pd_set->addTaxon(taxa_order[i]);
			area_id.addTaxon(i);
			taxon_coverage += *(area_taxa[i]);
			if (isBudgetConstraint())
				count += pda->getCost(i);
			else
				count++;
		}
	if (taxon_coverage.countTaxa() != getNTaxa()) {
	/*
		for (i = 0; i < nareas; i++)
			cout << variables[i] << endl;
	*/
		outError("Something wrong with LP in determining taxon coverage");
	}
	delete variables;
	return (count);
}


bool PDNetwork::checkAreaCoverage() {
	int ntaxa = getNTaxa();
	Split tax_cover(ntaxa);
	for (SplitSet::iterator it = area_taxa.begin(); it != area_taxa.end(); it++) {
		tax_cover += *(*it);
	}
	if (tax_cover.countTaxa() == ntaxa) {
		return true;
	}

	cout << "Warning: some taxa are not covered by any area including: ";
	for (int i = 0; i < ntaxa; i++)
		if (!tax_cover.containTaxon(i)) cout << taxa->GetTaxonLabel(i) << " ";
	cout << endl;
	return false;
}
