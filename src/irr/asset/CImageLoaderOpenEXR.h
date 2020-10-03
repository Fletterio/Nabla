// Copyright (C) 2018-2020 - DevSH Graphics Programming Sp. z O.O.
// This file is part of the "Nabla Engine".
// For conditions of distribution and use, see copyright notice in nabla.h

#ifndef __NBL_C_IMAGE_LOADER_OPENEXR__
#define __NBL_C_IMAGE_LOADER_OPENEXR__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_OPENEXR_LOADER_

#include "irr/asset/IImageLoader.h"

namespace irr
{
	namespace asset
	{	
		//! OpenEXR loader capable of loading .exr files
		class CImageLoaderOpenEXR final : public IImageLoader
		{
		protected:
			~CImageLoaderOpenEXR(){}

		public:
			CImageLoaderOpenEXR(IAssetManager* _manager) : m_manager(_manager) {}

			bool isALoadableFileFormat(io::IReadFile* _file) const override;

			const char** getAssociatedFileExtensions() const override
			{
				static const char* extensions[]{ "exr", nullptr };
				return extensions;
			}

			uint64_t getSupportedAssetTypesBitfield() const override { return asset::IAsset::ET_IMAGE; }

			asset::SAssetBundle loadAsset(io::IReadFile* _file, const asset::IAssetLoader::SAssetLoadParams& _params, asset::IAssetLoader::IAssetLoaderOverride* _override = nullptr, uint32_t _hierarchyLevel = 0u) override;

		private:

			IAssetManager* m_manager;
		};
	}
}

#endif // _IRR_COMPILE_WITH_OPENEXR_LOADER_
#endif // __C_IMAGE_LOADER_OPENEXR__
