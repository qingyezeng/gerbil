#ifndef COMPUTE_H
#define COMPUTE_H

// Boost multi_array has portability problems (at least for Boost 1.51 and below).
#ifndef __GNUC__
#pragma warning(disable:4996) // disable MSVC Checked Iterators warnings
#endif
#ifndef Q_MOC_RUN
#include <boost/multi_array.hpp> // ensure that multi_array is not visible to Qt MOC
#endif

// TODO: ask petr what this is all about
inline size_t tbb_size_t_select(unsigned u, unsigned long long ull) {
	return (sizeof(size_t) == sizeof(u)) ? size_t(u) : size_t(ull);
}
static const size_t tbb_hash_multiplier = tbb_size_t_select(2654435769U, 11400714819323198485ULL);

namespace tbb {

template<typename T>
inline size_t tbb_hasher(const boost::multi_array<T, 1> &a) {
	size_t h = 0;
	for (size_t i = 0; i < a.size(); ++i)
		h = static_cast<size_t>(a[i]) ^ (h * tbb_hash_multiplier);
	return h;
}

}

#include <multi_img.h>
#include <shared_data.h>

#include <QGLBuffer>
#include <QGLFramebufferObject>

#include <limits>
#include <tbb/atomic.h>
#include <tbb/concurrent_vector.h>
#include <tbb/concurrent_hash_map.h>
#include <tbb/task.h>
#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>
#include <tbb/partitioner.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/tbb_allocator.h>

/* N: number of bands,
 * D: number of bins per band (discretization steps)
 */
/* a Bin is an entry in our N-dimensional sparse histogram
 * it holds a representative vector and is identified by its
 * hash key (the hash key is not part of the Bin class
 */
struct Bin {
	Bin() : weight(0.f) {}
	Bin(const multi_img::Pixel& initial_means)
		: weight(1.f), means(initial_means) {} //, points(initial_means.size()) {}

	/* we store the mean/avg. of all pixel vectors represented by this bin
	 * the mean is not normalized during filling the bin, only afterwards
	 */
	inline void add(const multi_img::Pixel& p) {
		/* weight holds the number of pixels this bin represents
		 */
		weight += 1.f;
		if (means.empty())
			means.resize(p.size(), 0.f);
		std::transform(means.begin(), means.end(), p.begin(), means.begin(),
					   std::plus<multi_img::Value>());
	}

	/* in incremental update of our BinSet, we can also remove pixels from a bin */
	inline void sub(const multi_img::Pixel& p) {
		weight -= 1.f;
		assert(!means.empty());
		std::transform(means.begin(), means.end(), p.begin(), means.begin(),
					   std::minus<multi_img::Value>());
	}

	float weight;
	std::vector<multi_img::Value> means;
	/* each bin can have a color calculated for the mean vector
	 */
	QColor rgb;
};

struct BinSet {
	BinSet(const QColor &c, int size)
		: label(c), boundary(size, std::make_pair((int)255, (int)0)) { totalweight = 0; }
	/* each BinSet represents a label and has the label color
	 */
	QColor label;
	// FIXME Why boost::multi_array?
	/* each entry is a N-dimensional vector, discretized by one char per band
	 * this means that we can have at most D = 256
	 */
	typedef boost::multi_array<unsigned char, 1> HashKey;
	/* the hash map holds all representative vectors (of size N)
	 * the hash realizes a sparse histogram
	 */
	typedef tbb::concurrent_hash_map<HashKey, Bin> HashMap;
	HashMap bins;
	/* to set opacity value we normalize by total weight == sum of bin weights
	 * this is atomic to allow multi-threaded adding of vectors into the hash
	 */
	tbb::atomic<int> totalweight;
	/* the boundary is used for limiter mode initialization by label
	 * it is of length N and holds min, max bin indices occupied in each dimension
	 */
	std::vector<std::pair<int, int> > boundary;
};

typedef boost::shared_ptr<SharedData<std::vector<BinSet> > > sets_ptr;
typedef tbb::concurrent_vector<std::pair<int, BinSet::HashKey> > binindex;

/* TODO: this is here as both multi_img_viewer.h and viewport.h include
 * this header. but it sucks to have it here. */
enum representation {
	IMG = 0,
	GRAD = 1,
	IMGPCA = 2,
	GRADPCA = 3,
	REPSIZE = 4
};

std::ostream &operator<<(std::ostream& os, const representation& r);

struct ViewportCtx {
	ViewportCtx &operator=(const ViewportCtx &other) {
		wait = other.wait;
		reset = other.reset;
		dimensionality = other.dimensionality;
		dimensionalityValid = other.dimensionalityValid;
		type = other.type;
		meta = other.meta;
		metaValid = other.metaValid;
		labels = other.labels;
		labelsValid = other.labelsValid;
		ignoreLabels = other.ignoreLabels;
		nbins = other.nbins;
		binsize = other.binsize;
		binsizeValid = other.binsizeValid;
		minval = other.minval;
		minvalValid = other.minvalValid;
		maxval = other.maxval;
		maxvalValid = other.maxvalValid;
		return *this;
	}

	tbb::atomic<int> wait;
	tbb::atomic<int> reset;
	size_t dimensionality;
	bool dimensionalityValid;
	representation type;
	std::vector<multi_img::BandDesc> meta;
	bool metaValid;
	std::vector<QString> labels;
	bool labelsValid;
	bool ignoreLabels;
	int nbins;
	multi_img::Value binsize;
	bool binsizeValid;
	multi_img::Value minval;
	bool minvalValid;
	multi_img::Value maxval;
	bool maxvalValid;
};

typedef boost::shared_ptr<SharedData<ViewportCtx> > vpctx_ptr;

class Compute
{
public:

	/* method and helper class to preprocess bins before vertex generation */
	static void preparePolylines(const ViewportCtx &context,
								 std::vector<BinSet> &sets, binindex &index);

	class PreprocessBins {
	public:
		PreprocessBins(int label, size_t dimensionality, multi_img::Value maxval,
			const std::vector<multi_img::BandDesc> &meta,
			binindex &index)
			: label(label), dimensionality(dimensionality), maxval(maxval), meta(meta),
			index(index), ranges(dimensionality, std::pair<int, int>(INT_MAX, INT_MIN)) {}
		PreprocessBins(PreprocessBins &toSplit, tbb::split)
			: label(toSplit.label), dimensionality(toSplit.dimensionality),
			maxval(toSplit.maxval), meta(toSplit.meta),
			index(toSplit.index), ranges(dimensionality, std::pair<int, int>(INT_MAX, INT_MIN)) {}
		void operator()(const BinSet::HashMap::range_type &r);
		void join(PreprocessBins &toJoin);
		std::vector<std::pair<int, int> > GetRanges() { return ranges; }
	private:
		int label;
		size_t dimensionality;
		multi_img::Value maxval;
		const std::vector<multi_img::BandDesc> &meta;
		// pair of label index and hash-key within label's bin set
		binindex &index;
		std::vector<std::pair<int, int> > ranges;
	};

	/* method and helper class to extract and store vertice data from
	 * preprocessed bins */
	/// @returns error code or zero on success
	static int storeVertices(const ViewportCtx &context,
							 const std::vector<BinSet> &sets,
							 const binindex& index, QGLBuffer &vb,
							 bool drawMeans, bool illuminant_correction,
							 const std::vector<multi_img::Value> &illuminant);

	class GenerateVertices {
	public:
		GenerateVertices(bool drawMeans, size_t dimensionality, multi_img::Value minval, multi_img::Value binsize,
			bool illuminant_correction, const std::vector<multi_img::Value> &illuminant, const std::vector<BinSet> &sets,
			const binindex &index, GLfloat *varr)
			: drawMeans(drawMeans), dimensionality(dimensionality), minval(minval), binsize(binsize),
			illuminant_correction(illuminant_correction), illuminant(illuminant), sets(sets),
			index(index), varr(varr) {}
		void operator()(const tbb::blocked_range<size_t> &r) const;
	private:
		bool drawMeans;
		size_t dimensionality;
		multi_img::Value minval;
		multi_img::Value binsize;
		bool illuminant_correction;
		const std::vector<multi_img::Value> &illuminant;
		const std::vector<BinSet> &sets;
		const binindex &index;
		GLfloat *varr;
	};

	Compute();
};

#endif // COMPUTE_H