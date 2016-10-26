#include <animaReadWriteFunctions.h>
#include <tclap/CmdLine.h>

int main(int argc, const char** argv)
{
    TCLAP::CmdLine cmd("INRIA / IRISA - VisAGeS Team", ' ',ANIMA_VERSION);
    
    // Required arguments
    TCLAP::ValueArg<std::string> inArg("i","input","input 4D image",true,"","input image",cmd);
    TCLAP::ValueArg<std::string> outArg("o","output","Output 4D subset image.",true,"","output mask",cmd);
    TCLAP::ValueArg<std::string> idxArg("s","subset","File listing the indices of the subset.",true,"","subset indices",cmd);
    
    try
    {
        cmd.parse(argc,argv);
    }
    catch (TCLAP::ArgException& e)
    {
        std::cerr << "Error: " << e.error() << "for argument " << e.argId() << std::endl;
        return(1);
    }
    
    typedef itk::Image<float,4> Image4DType;
    typedef itk::ImageRegionConstIterator<Image4DType> InputIteratorType;
    typedef itk::ImageRegionIterator<Image4DType> OutputIteratorType;
    
    // Get info from input image
    Image4DType::Pointer inImage = anima::readImage<Image4DType>(inArg.getValue());
    Image4DType::RegionType inRegion = inImage->GetLargestPossibleRegion();
    unsigned int inNumImages = inRegion.GetSize()[3];
    
    // Get indices for subsetting
    std::ifstream idxFile(idxArg.getValue().c_str());
    
    if (!idxFile.is_open())
        throw itk::ExceptionObject(__FILE__, __LINE__,"Could not open the file containing the subset indices.",ITK_LOCATION);
    
    std::vector<unsigned int> idxVecs;
    if (idxFile.eof())
    {
        idxFile.close();
        throw itk::ExceptionObject(__FILE__, __LINE__,"File containing the subset indices is empty.",ITK_LOCATION);
    }
    
    std::string workStr;
    do
    {
        std::getline(idxFile,workStr);
    }
    while((workStr == "")&&(!idxFile.eof()));
    
    workStr.erase(workStr.find_last_not_of(" \n\r\t")+1);
    
    std::istringstream iss(workStr);
    std::string shortStr;
    do
    {
        iss >> shortStr;
        idxVecs.push_back(std::stoi(shortStr));
    }
    while (!iss.eof());
    
    idxFile.close();
    
    unsigned int numIndices = idxVecs.size();
    
    for (unsigned int i = 0;i < numIndices;++i)
        std::cout << idxVecs[i] << " ";
    std::cout << std::endl;
    
    // Create output image
    Image4DType::Pointer outImage = Image4DType::New();
    outImage->Initialize();
    outImage->SetSpacing(inImage->GetSpacing());
    outImage->SetOrigin(inImage->GetOrigin());
    outImage->SetDirection(inImage->GetDirection());
    
    Image4DType::RegionType::SizeType outSizes;
    for (unsigned int i = 0;i < 3;++i)
        outSizes[i] = inRegion.GetSize()[i];
    outSizes[3] = numIndices;
    
    Image4DType::RegionType::IndexType outIndices;
    for (unsigned int i = 0;i < 4;++i)
        outIndices[i] = inRegion.GetIndex()[i];
    
    Image4DType::RegionType outRegion(outIndices, outSizes);
    outImage->SetRegions(outRegion);
    outImage->Allocate();
    outImage->FillBuffer(0);
    
    for (unsigned int i = 0;i < numIndices;++i)
    {
        inRegion.SetIndex(3,idxVecs[i]);
        inRegion.SetSize(3,1);
        
        outRegion.SetIndex(3,i);
        outRegion.SetSize(3,1);
        
        InputIteratorType inItr(inImage,inRegion);
        OutputIteratorType outItr(outImage,outRegion);
        
        while (!outItr.IsAtEnd())
        {
            outItr.Set(inItr.Get());
            ++inItr;
            ++outItr;
        }
    }
    
    anima::writeImage<Image4DType>(outArg.getValue(), outImage);
    
    return 0;
}
