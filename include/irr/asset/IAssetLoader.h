#ifndef __IRR_I_ASSET_LOADER_H_INCLUDED__
#define __IRR_I_ASSET_LOADER_H_INCLUDED__

#include "irr/core/core.h"

#include "IAsset.h"
#include "IReadFile.h"

namespace irr { namespace asset
{

//! A class automating process of loading Assets from resources, eg. files
/**
	Every Asset must be loaded by a particular class derived from IAssetLoader.
	These classes must be registered with IAssetManager::addAssetLoader() which will 
	add it to the list of loaders (grab return 0-based index) or just not register 
	the loader upon failure (don�t grab and return 0xdeadbeefu).

	The loading is impacted by caching and resource duplication flags, defined as IAssetLoader::E_CACHING_FLAGS.

	There are defined rules of loading process, that can be overwritten, but basically
	a mesh can reference a submesh, a submesh can reference a material, a material 
	can reference a texture, etc. You can think about it as a \bMesh->Submesh->Material->Texture\b chain, 
	where a binding between them takes place. Furthermore they are indexed, so it actaully looks like \b0->1->2->3\b.
	
	Suppose user called IAssetManager::getAsset() and got Submesh during loading process,
	where currently loaded Asset is a Texture. In that case Submesh is treated as \broot\b of such a chain.
	Also there is a next important binding term that has to be considered - \bhierarchyLevel\b, called commonly "LEVEL".
	It specifies amount of shifts in a chain mentioned above between a root Asset got by user and currently loaded Asset.
	In case above - Submesh Asset is a root, so it has technically 1 index, and Texture Asset has 3.
	The difference between those values is 2, so \bhierarchyLevel\b becomes 2 too.

	The flag having an impact on loading an Asset is a bitfield with 2 bits per level,
	so the enums provie are some useful constants. Different combinations are valid as well, so
	
	\code{.cpp}
	static_cast<E_CACHING_FLAGS>(ECF_DONT_CACHE_TOP_LEVEL << 4ull) 
	\endcode

	Means that anything on level 2 will not get cached (top is 0, but we have shifted for 4 bits,
	where 2 bits represent one single level, so we've been on second level) 

	When the class derived from IAssetLoader is added, its put once on an 
	std::vector<IAssetLoader*> and once on an std::multimap<std::string,IAssetLoader*> 
	inside the IAssetManager for every associated file extension it reports.

	The loaders are tried in the order they were registered per file extensions, 
	and later in the global order in case of needing to fallback to examining files.

	An IAssetLoader can only be removed/deregistered by its original pointer or global loader index.

	@see IReferenceCounted::grab()
	@see IAsset
	@see IAssetManager
	@see IAssetWriter
*/

class IAssetLoader : public virtual core::IReferenceCounted
{
public:

	//! Caching and resource duplication flags 
	/**
		They have an impact on loading an Asset.

		E_CACHING_FLAGS::ECF_CACHE_EVERYTHING is default, means that asset you can find in previously cached asset will be just returned.
		There may occour that you can't, so it will be loaded and added to the cache before returning
		E_CACHING_FLAGS::ECF_DONT_CACHE_TOP_LEVEL means that master/parent is searched for in the caches, 
		but not added to the cache if not found and loaded
		E_CACHING_FLAGS::ECF_DUPLICATE_TOP_LEVEL means that master/parent object is loaded without searching
		for it in the cache, nor adding it to the cache after the load
		E_CACHING_FLAGS::ECF_DONT_CACHE_REFERENCES means that it concerns any asset that the top level asset refers to, such as a texture
		E_CACHING_FLAGS::ECF_DUPLICATE_REFERENCES means almost the same as E_CACHING_FLAGS::ECF_DUPLICATE_TOP_LEVEL but for any asset in the chain
	*/

    enum E_CACHING_FLAGS : uint64_t
    {
        ECF_CACHE_EVERYTHING = 0,
        ECF_DONT_CACHE_TOP_LEVEL = 0x1ull,						//!< master/parent is searched for in the caches, but not added to the cache if not found and loaded   
        ECF_DUPLICATE_TOP_LEVEL = 0x3ull,						//!< master/parent object is loaded without searching for it in the cache, nor adding it to the cache after the load
        ECF_DONT_CACHE_REFERENCES = 0x5555555555555555ull,		//!< this concerns any asset that the top level asset refers to, such as a texture
        ECF_DUPLICATE_REFERENCES = 0xffffffffffffffffull		//!< meaning identical as to ECF_DUPLICATE_TOP_LEVEL but for any asset in the chain
    };

	//! Struct storing important data used for Asset loading process
	/**
		Struct stores a key decryptionKey for potentially encrypted files, it is used to decrypt them. You can find an usage
		example in CBAWMeshFileLoader .cpp file. Since decryptionKey is a pointer, size must be specified 
		for iterating through key properly and decryptionKeyLen stores it.
		Current flags set by user that defines rules during loading process are stored in cacheFlags.

		@see CBAWMeshFileLoader
		@see E_CACHING_FLAGS
	*/

    struct SAssetLoadParams
    {
        SAssetLoadParams(const size_t& _decryptionKeyLen = 0u, const uint8_t* _decryptionKey = nullptr, const E_CACHING_FLAGS& _cacheFlags = ECF_CACHE_EVERYTHING)
            : decryptionKeyLen(_decryptionKeyLen), decryptionKey(_decryptionKey), cacheFlags(_cacheFlags)
        {
        }
        size_t decryptionKeyLen;			 //!< The size of decryptionKey
        const uint8_t* decryptionKey;		 //!< The key it used to decrypt potentially encrypted files
        const E_CACHING_FLAGS cacheFlags;	 //!< Flags defining rules during loading process
    };

    //! Struct for keeping the state of the current loadoperation for safe threading
	/**
		Important data used for Asset loading process is stored by params.
		Also a path to Asset data file is specified, stored by mainFile. You can store path
		to file as an absolute path or a relative path, flexibility is provided.

		@see SAssetLoadParams
	*/

    struct SAssetLoadContext
    {
        const SAssetLoadParams params;		//!< Data used for Asset loading process
        io::IReadFile* mainFile;			//!< A path to Asset data file
    };

    // following could be inlined
    static E_CACHING_FLAGS ECF_DONT_CACHE_LEVEL(uint64_t N)
    {
        N *= 2ull;
        return (E_CACHING_FLAGS)(ECF_DONT_CACHE_TOP_LEVEL << N);
    }
    static E_CACHING_FLAGS ECF_DUPLICATE_LEVEL(uint64_t N)
    {
        N *= 2ull;
        return (E_CACHING_FLAGS)(ECF_DUPLICATE_TOP_LEVEL << N);
    }
    static E_CACHING_FLAGS ECF_DONT_CACHE_FROM_LEVEL(uint64_t N)
    {
        // (Criss) Shouldn't be set all DONT_CACHE bits from hierarchy numbers N-1 to 32 (64==2*32) ? Same for ECF_DUPLICATE_FROM_LEVEL below
        N *= 2ull;
        return (E_CACHING_FLAGS)(ECF_DONT_CACHE_REFERENCES << N);
    }
    static E_CACHING_FLAGS ECF_DUPLICATE_FROM_LEVEL(uint64_t N)
    {
        N *= 2ull;
        return (E_CACHING_FLAGS)(ECF_DUPLICATE_REFERENCES << N);
    }
    static E_CACHING_FLAGS ECF_DONT_CACHE_UNTIL_LEVEL(uint64_t N)
    {
        // (Criss) is this ok? Shouldn't be set all DONT_CACHE bits from hierarchy numbers 0 to N-1? Same for ECF_DUPLICATE_UNTIL_LEVEL below
        N = 64ull - N * 2ull;
        return (E_CACHING_FLAGS)(ECF_DONT_CACHE_REFERENCES >> N);
    }
    static E_CACHING_FLAGS ECF_DUPLICATE_UNTIL_LEVEL(uint64_t N)
    {
        N = 64ull - N * 2ull;
        return (E_CACHING_FLAGS)(ECF_DUPLICATE_REFERENCES >> N);
    }

	//! Class for user to override functions to facilitate changing the way assets are loaded
	/**
		Each loader may override those functions to get more control on some process, but default implementations are provided.
		It handles such operations like finding Assets cached so far, inserting them to cache, getting path to
		file with Asset data, handling predefined opeartions if Assets searching fails or loading them, etc.
	*/

    class IAssetLoaderOverride
    {
    protected:
        IAssetManager* m_manager;
    public:
        IAssetLoaderOverride(IAssetManager* _manager) : m_manager(_manager) {}

        // The only reason these functions are not declared static is to allow stateful overrides

        //! The most imporant overrides are the ones for caching
        virtual SAssetBundle findCachedAsset(const std::string& inSearchKey, const IAsset::E_TYPE* inAssetTypes, const SAssetLoadContext& ctx, const uint32_t& hierarchyLevel);

        //! Since more then one asset of the same key of the same type can exist, this function is called right after search for cached assets (if anything was found) and decides which of them is relevant.
        //! Note: this function can assume that `found` is never empty.
        inline virtual SAssetBundle chooseRelevantFromFound(const core::vector<SAssetBundle>& found, const SAssetLoadContext& ctx, const uint32_t& hierarchyLevel)
        {
            return found.front();
        }

        //! Only called when the asset was searched for, no correct asset was found
        /** Any non-nullptr asset returned here will not be added to cache,
        since the overload operates �as if� the asset was found. */
        inline virtual SAssetBundle handleSearchFail(const std::string& keyUsed, const SAssetLoadContext& ctx, const uint32_t& hierarchyLevel)
        {
            return {};
        }

        //! Called before loading a file 
		/**
			\param inOutFilename is a path to file Asset data needs to correspond with. A path changes over time for each dependent resource.
			Actually, override decides how to resolve a local path or even a URL into a "proper" filename.
			\param ctx provides data required for loading process.
			\param hierarchyLevel specifies how deep are we being in some referenced-struct-data in a file,
			but it is more like a stack counter.

			Expected behaviour is that Asset loading will get called recursively. For instance mesh needs material, material needs texture, etc.
			GetLoadFilename() could be called separately for each dependent resource from deeper recursions in the loading stack.
			So inOutFilename is provided, and if path to a texture needed by a material is required - inOutFilename will store it after calling the function.

			More information about hierarchy level you can find at IAssetLoader description.

			@see IAssetLoader
			@see SAssetLoadContext
		*/
        inline virtual void getLoadFilename(std::string& inOutFilename, const SAssetLoadContext& ctx, const uint32_t& hierarchyLevel) {} //default do nothing

        inline virtual io::IReadFile* getLoadFile(io::IReadFile* inFile, const std::string& supposedFilename, const SAssetLoadContext& ctx, const uint32_t& hierarchyLevel)
        {
            return inFile;
        }
        // I would really like to merge getLoadFilename and getLoadFile into one function!

        //! When you sometimes have different passwords for different assets
        /** \param inOutDecrKeyLen expects length of buffer `outDecrKey`, then function writes into it length of actual key.
                Write to `outDecrKey` happens only if output value of `inOutDecrKeyLen` is less or equal to input value of `inOutDecrKeyLen`.
        \param supposedFilename is the string after modification by getLoadFilename.
        \param attempt if decryption or validation algorithm supports reporting failure, you can try different key*/
        inline virtual bool getDecryptionKey(uint8_t* outDecrKey, size_t& inOutDecrKeyLen, const uint32_t& attempt, const io::IReadFile* assetsFile, const std::string& supposedFilename, const std::string& cacheKey, const SAssetLoadContext& ctx, const uint32_t& hierarchyLevel)
        {
            if (ctx.params.decryptionKeyLen <= inOutDecrKeyLen)
                memcpy(outDecrKey, ctx.params.decryptionKey, ctx.params.decryptionKeyLen);
            inOutDecrKeyLen = ctx.params.decryptionKeyLen;
            return attempt == 0u; // no failed attempts
        }

        //! Only called when the was unable to be loaded
        inline virtual SAssetBundle handleLoadFail(bool& outAddToCache, const io::IReadFile* assetsFile, const std::string& supposedFilename, const std::string& cacheKey, const SAssetLoadContext& ctx, const uint32_t& hierarchyLevel)
        {
            outAddToCache = false; // if you want to return a �default error asset�
            return SAssetBundle();
        }

        //! After a successful load of an asset or sub-asset
        //TODO change name
        virtual void insertAssetIntoCache(SAssetBundle& asset, const std::string& supposedKey, const SAssetLoadContext& ctx, const uint32_t& hierarchyLevel);
    };

public:
    //! Check if the file might be loaded by this class
    /** Check might look into the file.
    \param file File handle to check.
    \return True if file seems to be loadable. */
    virtual bool isALoadableFileFormat(io::IReadFile* _file) const = 0;

    //! Returns an array of string literals terminated by nullptr
    virtual const char** getAssociatedFileExtensions() const = 0;

    //! Returns the assets loaded by the loader
    /** Bits of the returned value correspond to each IAsset::E_TYPE
    enumeration member, and the return value cannot be 0. */
    virtual uint64_t getSupportedAssetTypesBitfield() const { return 0; }

    //! Loads an asset from an opened file, returns nullptr in case of failure.
    virtual SAssetBundle loadAsset(io::IReadFile* _file, const SAssetLoadParams& _params, IAssetLoaderOverride* _override = nullptr, uint32_t _hierarchyLevel = 0u) = 0;

protected:
    // accessors for loaders
    SAssetBundle interm_getAssetInHierarchy(IAssetManager* _mgr, io::IReadFile* _file, const std::string& _supposedFilename, const IAssetLoader::SAssetLoadParams& _params, uint32_t _hierarchyLevel, IAssetLoader::IAssetLoaderOverride* _override);
    SAssetBundle interm_getAssetInHierarchy(IAssetManager* _mgr, const std::string& _filename, const IAssetLoader::SAssetLoadParams& _params, uint32_t _hierarchyLevel, IAssetLoader::IAssetLoaderOverride* _override);
    SAssetBundle interm_getAssetInHierarchy(IAssetManager* _mgr, io::IReadFile* _file, const std::string& _supposedFilename, const IAssetLoader::SAssetLoadParams& _params, uint32_t _hierarchyLevel);
    SAssetBundle interm_getAssetInHierarchy(IAssetManager* _mgr, const std::string& _filename, const IAssetLoader::SAssetLoadParams& _params, uint32_t _hierarchyLevel);
};

}}

#endif
