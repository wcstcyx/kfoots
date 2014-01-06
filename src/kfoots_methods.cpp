#include <Rcpp.h>
#include <boost/unordered_map.hpp>
#include "core.hpp"

// [[Rcpp::export]]
Rcpp::List llik2posteriors(Rcpp::NumericMatrix lliks, int nthreads=1){
	Rcpp::NumericMatrix posteriors(lliks.nrow(), lliks.ncol());
	double tot = llik2posteriors_core(asMat<double>(lliks), asMat<double>(posteriors), nthreads);
	
	return Rcpp::List::create(Rcpp::Named("posteriors")=posteriors, Rcpp::Named("tot_llik")=tot);
}

//' Group unique values of a vector
//'
//' @param v a vector of integers. If they are not integers they will be
//' 	casted to integers.
//' @return a list with the following items:
//'		\item{values}{unique and sorted values of \code{v}}
//'		\item{map}{a vector such that \code{v[i] = values[map[i]+1]} for every i}
//'	@export
// [[Rcpp::export]]
Rcpp::List mapToUnique(Rcpp::IntegerVector values){
	Rcpp::IntegerVector map(values.length());
	
	Vec<int> valuesVec = asVec<int>(values);
	Vec<int> mapVec = asVec<int>(map);
	std::vector<int> uniqueCS;
	map2unique_core(valuesVec, mapVec, uniqueCS);
	
	Rcpp::List list =  Rcpp::List::create(Rcpp::Named("values")=Rcpp::IntegerVector(uniqueCS.begin(),uniqueCS.end()), Rcpp::Named("map")=map);
	return list;
}



// [[Rcpp::export]]
Rcpp::NumericVector getMultinomConst(Rcpp::IntegerMatrix counts, int nthreads=1){
	Rcpp::NumericVector multinomConst(counts.ncol());
	getMultinomConst_core(asMat<int>(counts), asVec<double>(multinomConst), nthreads);
	return multinomConst;
}

typedef Rcpp::NumericVector::iterator diter;
typedef Rcpp::IntegerVector::iterator iiter;
// [[Rcpp::export]]
Rcpp::NumericVector sumAt(Rcpp::NumericVector values, Rcpp::IntegerVector map, int size, bool zeroIdx=false){
	Rcpp::NumericVector res(size);
	diter vend = values.end();
	diter vstart = values.begin();
	iiter mstart = map.begin();
	
	if (zeroIdx){
		for (; vstart!=vend; ++vstart, ++mstart){
			res[(*mstart)] += *vstart;
		}
	} else {
		for (; vstart!=vend; ++vstart, ++mstart){
			res[(*mstart)-1] += *vstart;
		}

	}
	return res;
}


// [[Rcpp::export]]
Rcpp::IntegerVector colSumsInt(Rcpp::IntegerMatrix nums, int nthreads=1){
	Mat<int> mat = asMat<int>(nums);
	Rcpp::IntegerVector ret(mat.ncol);
	Vec<int> vec = asVec<int>(ret);
	
	colSums(mat, vec, nthreads);
	return ret;
}

// [[Rcpp::export]]
Rcpp::NumericVector colSumsDouble(Rcpp::NumericMatrix nums, int nthreads=1){
	Mat<double> mat = asMat<double>(nums);
	Rcpp::NumericVector ret(mat.ncol);
	Vec<double> vec = asVec<double>(ret);
	
	colSums(mat, vec, nthreads);
	return ret;
}

// [[Rcpp::export]]
Rcpp::NumericVector nbinomLoglik(Rcpp::IntegerVector counts, double mu, double r, int nthreads=1){
	Rcpp::NumericVector res(counts.length());
	nbinomLoglik_core(asVec<int>(counts), mu, r, asVec<double>(res), std::max(nthreads, 1));
	return res;
}

// [[Rcpp::export]]
double optimFun(Rcpp::IntegerVector counts, double mu, double r, Rcpp::NumericVector posteriors, int nthreads=1){
	return optimFun_core(asVec<int>(counts), mu, r, asVec<double>(posteriors), std::max(nthreads, 1));
}

// [[Rcpp::export]]
Rcpp::NumericVector fitMultinom(Rcpp::IntegerMatrix counts, Rcpp::NumericVector posteriors, int nthreads=1){
	Rcpp::NumericVector fit(counts.nrow());
	fitMultinom_core(asMat<int>(counts), asVec<double>(posteriors), asVec<double>(fit), std::max(nthreads, 1));
	return fit;
}

static inline NegMultinom parseModel(Rcpp::List model){
	Rcpp::NumericVector ps = model["ps"];
	return NegMultinom(model["r"], model["mu"], Vec<double>(ps.begin(), ps.length()));
}

static inline void parseModels(Rcpp::List models, Vec<double> mus, Vec<double> rs, Mat<double> ps){
	unsigned int footsize = sizeof(double)*ps.nrow;
	for (int i = 0; i < models.length(); ++i){
		Rcpp::List model = models[i];
		mus[i] = model["mu"]; rs[i] = model["r"];
		Rcpp::NumericVector currps = model["ps"];
		memcpy(ps.colptr(i), currps.begin(), footsize);
	}
}

// [[Rcpp::export]]
Rcpp::NumericVector lLik(Rcpp::IntegerMatrix counts, Rcpp::List model, 
		SEXP ucs = R_NilValue,
		SEXP mConst = R_NilValue,
		int nthreads=1){
	
	
	if (Rf_isNull(ucs)){
		ucs = (SEXP) mapToUnique(colSumsInt(counts, nthreads));
	}
	if (Rf_isNull(mConst)){
		mConst = (SEXP) getMultinomConst(counts, nthreads);
	}
	
	Rcpp::List ucs_list(ucs); 
	Rcpp::IntegerVector uniqueCS = ucs_list["values"];
	Rcpp::IntegerVector map = ucs_list["map"];
	Rcpp::NumericVector multinomConst(mConst);
	
	Mat<int> countsMat = asMat<int>(counts);
	Rcpp::NumericVector lliks(counts.ncol());
	Vec<double> lliksVec = asVec<double>(lliks);
	NegMultinom NMmodel = parseModel(model);
	
	//re-format preprocessing data if present, otherwise, create it.
	//If created here they will not be persistent
	NMPreproc preproc(asVec<int>(uniqueCS), asVec<int>(map), asVec<double>(multinomConst));
	
	NMmodel.getLlik(countsMat, lliksVec, preproc, nthreads);
	
	return lliks;
}


// [[Rcpp::export]]
Rcpp::NumericMatrix lLikMat(Rcpp::IntegerMatrix counts, Rcpp::List models, 
		SEXP ucs = R_NilValue,
		SEXP mConst = R_NilValue,
		int nthreads=1){
	
	//parse or compute preprocessing data
	if (Rf_isNull(ucs)){
		ucs = (SEXP) mapToUnique(colSumsInt(counts, nthreads));
	}
	if (Rf_isNull(mConst)){
		mConst = (SEXP) getMultinomConst(counts, nthreads);
	}
	
	Rcpp::List ucs_list(ucs); 
	Rcpp::IntegerVector uniqueCS = ucs_list["values"];
	Rcpp::IntegerVector map = ucs_list["map"];
	Rcpp::NumericVector multinomConst(mConst);
	NMPreproc preproc(asVec<int>(uniqueCS), asVec<int>(map), asVec<double>(multinomConst));
	
	Mat<int> countsMat = asMat<int>(counts);
	//parsing the models
	int nmodels = models.length();
	int footlen = countsMat.nrow;
	std::vector<double> musSTD(nmodels);
	std::vector<double> rsSTD(nmodels);
	std::vector<double> psSTD(nmodels*footlen);
	Vec<double> mus = asVec(musSTD);
	Vec<double> rs = asVec(rsSTD);
	Mat<double> ps = asMat(psSTD, nmodels);
	parseModels(models, mus, rs, ps);
	//allocating some temporary memory
	std::vector<double> tmpNB(uniqueCS.length()*nmodels);
	//allocating return variable
	Rcpp::NumericMatrix lliks(nmodels, countsMat.ncol);
	
	lLikMat_core(countsMat, mus, rs, ps, asMat<double>(lliks), preproc, asMat(tmpNB, uniqueCS.length()), nthreads);
	
	return lliks;
}

