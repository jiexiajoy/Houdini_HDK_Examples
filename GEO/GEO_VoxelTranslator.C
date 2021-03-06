/*
 * Copyright (c) 2015
 *	Side Effects Software Inc.  All rights reserved.
 *
 * Redistribution and use of Houdini Development Kit samples in source and
 * binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. The name of Side Effects Software may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SIDE EFFECTS SOFTWARE `AS IS' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL SIDE EFFECTS SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *----------------------------------------------------------------------------
 */

#include <UT/UT_DSOVersion.h>

#include <GU/GU_Detail.h>
#include <GU/GU_PrimVolume.h>
#include <GEO/GEO_AttributeHandle.h>
#include <GEO/GEO_IOTranslator.h>
#include <SOP/SOP_Node.h>
#include <UT/UT_Assert.h>
#include <UT/UT_IOTable.h>

#include <iostream>
#include <stdio.h>


namespace HDK_Sample {

class GEO_VoxelIOTranslator : public GEO_IOTranslator
{
public:
	     GEO_VoxelIOTranslator() {}
	     GEO_VoxelIOTranslator(const GEO_VoxelIOTranslator &src) {}
    virtual ~GEO_VoxelIOTranslator() {}

    virtual GEO_IOTranslator	*duplicate() const;

    virtual const char *formatName() const;

    virtual int		checkExtension(const char *name);

    virtual int		checkMagicNumber(unsigned magic);

    virtual GA_Detail::IOStatus	 fileLoad(GEO_Detail *, UT_IStream &,
					  bool ate_magic);
    virtual GA_Detail::IOStatus	 fileSave(const GEO_Detail *, std::ostream &);
};

}

using namespace HDK_Sample;

GEO_IOTranslator *
GEO_VoxelIOTranslator::duplicate() const
{
    return new GEO_VoxelIOTranslator(*this);
}

const char *
GEO_VoxelIOTranslator::formatName() const
{
    return "Silly Sample Voxel Format";
}

int
GEO_VoxelIOTranslator::checkExtension(const char *name) 
{
    UT_String		sname(name);

    if (sname.fileExtension() && !strcmp(sname.fileExtension(), ".voxel"))
	return true;
    return false;
}

int
GEO_VoxelIOTranslator::checkMagicNumber(unsigned magic)
{
    return 0;
}

GA_Detail::IOStatus
GEO_VoxelIOTranslator::fileLoad(GEO_Detail *gdp, UT_IStream &is, bool ate_magic)
{
    // Convert our stream to ascii.
    UT_IStreamAutoBinary	forceascii(is, false);

    // Check our magic token
    if (!is.checkToken("VOXELS"))
	return GA_Detail::IOStatus(false);

#if defined(HOUDINI_11)
    int				def = -1;
    gdp->addPrimAttrib("name", sizeof(int), GB_ATTRIB_INDEX, &def);
#endif
    gdp->addStringTuple(GA_ATTRIB_PRIMITIVE, "name", 1);
    GA_RWHandleS name_attrib(gdp->findPrimitiveAttribute("name"));

    while (is.checkToken("VOLUME"))
    {
	UT_String		name;
	UT_WorkBuffer		buf;

	is.getWord(buf);
	name.harden(buf.buffer());

	int			rx, ry, rz;

	is.read(&rx); is.read(&ry); is.read(&rz);

	// Center and size
	float			tx, ty, tz, sx, sy, sz;

	is.read<fpreal32>(&tx); is.read<fpreal32>(&ty); is.read<fpreal32>(&tz);
	is.read<fpreal32>(&sx); is.read<fpreal32>(&sy); is.read<fpreal32>(&sz);

	GU_PrimVolume		*vol;

	vol = (GU_PrimVolume *)GU_PrimVolume::build((GU_Detail *)gdp);

	// Set the name of the primitive
	name_attrib.set(vol->getMapOffset(), name);

	// Set the center of the volume
#if defined(HOUDINI_11)
	vol->getVertex().getPos() = UT_Vector3(tx, ty, tz);
#else
	gdp->setPos3(vol->getPointOffset(0), UT_Vector3(tx, ty, tz));
#endif

	UT_Matrix3		xform;

	// The GEO_PrimVolume treats the voxel array as a -1 to 1 cube
	// so its size is 2, so we scale by 0.5 here.
	xform.identity();
	xform.scale(sx/2, sy/2, sz/2);

	vol->setTransform(xform);

	UT_VoxelArrayWriteHandleF	handle = vol->getVoxelWriteHandle();

	// Resize the array.
	handle->size(rx, ry, rz);

	if (!is.checkToken("{"))
	    return GA_Detail::IOStatus(false);

	for (int z = 0; z < rz; z++)
	{
	    for (int y = 0; y < ry; y++)
	    {
		for (int x = 0; x < rx; x++)
		{
		    float		v;

		    is.read<fpreal32>(&v);

		    handle->setValue(x, y, z, v);
		}
	    }
	}

	if (!is.checkToken("}"))
	    return GA_Detail::IOStatus(false);

	// Proceed to the next volume.
    }

    // All done successfully
    return GA_Detail::IOStatus(true);
}

GA_Detail::IOStatus
GEO_VoxelIOTranslator::fileSave(const GEO_Detail *gdp, std::ostream &os)
{
    // Write our magic token.
    os << "VOXELS" << std::endl;

    // Now, for each volume in our gdp...
    UT_WorkBuffer buf;

    GA_ROHandleS name_attrib(gdp->findPrimitiveAttribute("name"));
    const GEO_Primitive *prim;
#if defined(HOUDINI_11)
    FOR_ALL_PRIMITIVES(gdp, prim)
#else
    GA_FOR_ALL_PRIMITIVES(gdp, prim)
#endif
    {
#if defined(HOUDINI_11)
	if (prim->getPrimitiveId() == GEOPRIMVOLUME)
	// alternatively, this could be ported as
	// if (prim->getPrimitiveId() == GEO_PrimTypeCompat::GEOPRIMVOLUME)
#else
	if (prim->getTypeId() == GEO_PRIMVOLUME)
#endif
	{
	    // Default name
#if defined(HOUDINI_11)
	    // Primitive numbers can now be 64 bit
	    buf.sprintf("volume_%d", prim->getNum());
#else
	    buf.sprintf("volume_%" SYS_PRId64, (exint)prim->getMapIndex());
#endif
            UT_String name;
	    name.harden(buf.buffer());

	    // Which is overridden by any name attribute.
	    if (name_attrib.isValid())
		name = name_attrib.get(prim->getMapOffset());

	    os << "VOLUME " << name << std::endl;
	    const GEO_PrimVolume *vol = (const GEO_PrimVolume *)prim;

	    // Save resolution
	    int resx, resy, resz;
	    vol->getRes(resx, resy, resz);
	    os << resx << " " << resy << " " << resz << std::endl;

	    // Save the center and approximate size.
	    // Calculating the size is complicated as we could be rotated
	    // or sheared.  We lose all these because the .voxel format
	    // only supports aligned arrays.
	    UT_Vector3		p1, p2;

	    UT_Vector3 tmp = vol->getPos3(0);
	    os << tmp.x() << " " << tmp.y() << " " << tmp.z() << std::endl;

	    vol->indexToPos(0, 0, 0, p1);
	    vol->indexToPos(1, 0, 0, p2);
            float length = (p1 - p2).length();
	    os << resx * length << " ";
	    vol->indexToPos(0, 1, 0, p2);
	    os << resy * length << " ";
	    vol->indexToPos(0, 0, 1, p2);
	    os << resz * length << std::endl;

	    UT_VoxelArrayReadHandleF handle = vol->getVoxelHandle();

	    // Enough of a header, dump the data.
	    os << "{" << std::endl;
	    for (int z = 0; z < resz; z++)
	    {
		for (int y = 0; y < resy; y++)
		{
		    os << "    ";
		    for (int x = 0; x < resx; x++)
		    {
			os << (*handle)(x, y, z) << " ";
		    }
		    os << std::endl;
		}
	    }
	    os << "}" << std::endl;
	    os << std::endl;
	}
    }

    return GA_Detail::IOStatus(true);
}

void
newGeometryIO(void *)
{
    GU_Detail::registerIOTranslator(new GEO_VoxelIOTranslator());

    // Note due to the just-in-time loading of GeometryIO, the f3d
    // won't be added until after your first f3d save/load.
    // Thus this is replicated in the newDriverOperator.
    UT_ExtensionList		*geoextension;
    geoextension = UTgetGeoExtensions();
    if (!geoextension->findExtension("voxel"))
	geoextension->addExtension("voxel");
}
